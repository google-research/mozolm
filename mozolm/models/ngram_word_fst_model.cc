// Copyright 2021 MozoLM Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mozolm/models/ngram_word_fst_model.h"

#include <algorithm>
#include <cmath>

#include "google/protobuf/stubs/logging.h"
#include "fst/fst.h"
#include "fst/matcher.h"
#include "fst/symbol-table.h"
#include "ngram/ngram-model.h"
#include "absl/memory/memory.h"
#include "mozolm/models/model_storage.pb.h"
#include "mozolm/models/ngram_word_fst_options.pb.h"
#include "nisaba/port/utf8_util.h"
#include "nisaba/port/status_macros.h"

using nisaba::utf8::DecodeSingleUnicodeChar;
using nisaba::utf8::EncodeUnicodeChar;
using nisaba::utf8::StrSplitByChar;

using absl::StatusOr;
using fst::ArcIterator;
using fst::MATCH_INPUT;
using fst::Matcher;
using fst::StdArc;
using fst::StdVectorFst;
using fst::SymbolTable;
using fst::Times;

namespace mozolm {
namespace models {
namespace impl {
namespace {

double SafeNegLogDiff(double cost1, double cost2) {
  if (cost1 >= cost2) {
    return StdArc::Weight::Zero().Value();
  }
  return ngram::NegLogDiff(cost1, cost2);
}

// Returns the character at index idx in the unicode string.
std::string GetNextChar(const std::string& sym, int idx) {
  const std::vector<std::string> syms = StrSplitByChar(sym);
  if (idx < 0 || syms.size() <= idx) {
    // No symbol at that index for the input string, returns whitespace.
    return " ";
  }
  return syms[idx];
}

}  // namespace
}  // namespace impl

absl::Status NGramWordFstModel::EstablishLexicographicOrdering() {
  const SymbolTable *syms = fst().InputSymbols();
  std::vector<std::string> symbols(syms->NumSymbols());
  for (StdArc::Label i = 0; i < symbols.size(); ++i) {
    symbols[i] = syms->Find(i);
  }
  std::sort(symbols.begin(), symbols.end());
  lexicographic_order_.resize(symbols.size());
  lexicographic_position_.resize(symbols.size());
  previous_common_prefix_length_.resize(symbols.size());
  // By convention, put <epsilon> symbol initially, since we won't use that
  // position for symbol ranges.
  int idx = 0;
  previous_common_prefix_length_[idx] = 0;
  lexicographic_order_[idx] = 0;
  lexicographic_position_[0] = idx++;
  first_char_begin_index_ = idx;
  std::string first_char;
  std::vector<std::string> last_string(1);
  for (int i = 0; i < symbols.size(); ++i) {
    StdArc::Label sym = syms->Find(symbols[i]);
    if (sym != 0 && sym != oov_label()) {
      const std::vector<std::string> this_string = StrSplitByChar(symbols[i]);
      int prefix_match = 0;
      int min_len = std::min(last_string.size(), this_string.size());
      while (prefix_match < min_len &&
             last_string[prefix_match] == this_string[prefix_match]) {
        ++prefix_match;
      }
      if (prefix_match == 0) {
        // No prefix overlap, hence new first letter of a word.
        if (idx > 1) {
          // Records last index of previous character.
          first_char_ends_.push_back(idx - 1);
        }
        const std::string first_char =
            this_string.empty() ? "" : this_string[0];
        first_chars_.push_back(first_char);
      }
      previous_common_prefix_length_[idx] = prefix_match;
      lexicographic_order_[idx] = sym;
      lexicographic_position_[sym] = idx++;
      last_string = this_string;
    }
  }
  first_char_ends_.push_back(idx - 1);
  if (oov_label() >= 0) {
    // By convention, put oov_label() last if it exists, since we won't use that
    // symbol in our calculations.
    lexicographic_order_[idx] = oov_label();
    previous_common_prefix_length_[idx] = 0;
    lexicographic_order_[oov_label()] = idx++;
  }
  if (idx != symbols.size()) {
    return absl::InternalError("Symbol table for model is not dense");
  }
  ngram_implicit_states_ = absl::make_unique<NGramImplicitStates>(
      fst(), first_char_begin_index_, first_char_ends_.back());

  // Creates an implicit state for out-of-vocabulary words, which then
  // transitions to the unigram state at a word boundary.
  StatusOr<int> oov_state = ngram_implicit_states_->GetState(
      /*model_state=*/-1, /*prefix_length=*/1, /*symbol_begin_index=*/-1,
      /*symbol_end_index=*/-1);
  if (!oov_state.ok()) {
    return absl::InternalError("Could not establish OOV state");
  }
  oov_state_ = *oov_state;
  return absl::OkStatus();
}

std::vector<double> NGramWordFstModel::FillWeightVector(int state) {
  if (cache_index_[state] >= 0) {
    // State is already cached, returning the probabilities.
    return state_cache_[cache_index_[state]]->cummulative_neg_log_probs();
  }
  std::vector<double> weights(lexicographic_order_.size(),
                              StdArc::Weight::Zero().Value());
  std::vector<bool> weights_here(weights.size(), false);
  for (ArcIterator<StdVectorFst> arc_iterator(fst(), state);
       !arc_iterator.Done(); arc_iterator.Next()) {
    const StdArc arc = arc_iterator.Value();
    if (arc.ilabel > 0) {
      weights[lexicographic_position_[arc.ilabel]] = arc.weight.Value();
      weights_here[lexicographic_position_[arc.ilabel]] = true;
    }
  }
  StdArc::Weight backoff_weight;
  const int backoff_state = model_->GetBackoff(state, &backoff_weight);
  std::vector<double> backoff_weights;
  if (backoff_state >= 0) {
    // Gets vector from backoff state.
    absl::Status status = EnsureCacheIndex(backoff_state);
    if (status == absl::OkStatus()) {
      backoff_weights = FillWeightVector(backoff_state);
      state_cache_[cache_index_[backoff_state]]->set_last_accessed(
          cache_accessed_++);
    }
  }
  if (backoff_weights.empty()) {
    // Marks all items as having weight drawn from this state.
    weights_here.clear();
    weights_here.resize(weights.size(), true);
  }
  if (fst().Final(state) != StdArc::Weight::Zero()) {
    // By convention, index 0 is end-of-string probability.
    weights[0] = fst().Final(state).Value();
  } else if (!backoff_weights.empty()) {
    weights[0] = backoff_weights[0] + backoff_weight.Value();
  }
  double kahan_value = 0.0;
  for (int i = 1; i < weights.size(); ++i) {
    if (!weights_here[i]) {
      // Derives backoff probability by unsumming backoff cummulative weights.
      weights[i] =
          backoff_weight.Value() +
          impl::SafeNegLogDiff(backoff_weights[i], backoff_weights[i - 1]);
    }
    // Converts to cummulative weights for ease of later aggregation.
    weights[i] = ngram::NegLogSum(weights[i], weights[i - 1], &kahan_value);
  }
  return weights;
}

int NGramWordFstModel::FindOldestLastAccessedCache() const {
  int least_accessed_cache = 0;
  int oldest_access = state_cache_[0]->last_accessed();
  for (size_t i = 1; i < state_cache_.size(); ++i) {
    if (state_cache_[i]->last_accessed() < oldest_access) {
      least_accessed_cache = i;
      oldest_access = state_cache_[i]->last_accessed();
    }
  }
  return least_accessed_cache;
}

absl::Status NGramWordFstModel::GetNewCacheIndex(
    StdArc::StateId s, const std::vector<double> &weights) {
  if (state_cache_.size() < max_cache_size_) {
    cache_index_[s] = state_cache_.size();
    state_cache_.push_back(
        std::make_unique<NGramStateCache>(s, cache_accessed_++, weights));
  } else {
    const int index_to_update = FindOldestLastAccessedCache();
    const StdArc::StateId old_state = state_cache_[index_to_update]->state();
    if (cache_index_[old_state] != index_to_update) {
      return absl::InternalError("Cache index not updated correctly.");
    }
    cache_index_[old_state] = -1;
    state_cache_[index_to_update] =
        std::make_unique<NGramStateCache>(s, cache_accessed_++, weights);
    cache_index_[s] = index_to_update;
  }
  return absl::OkStatus();
}

absl::Status NGramWordFstModel::EnsureCacheIndex(int state) {
  if (cache_index_[state] >= 0) {
    return absl::OkStatus();
  }
  const std::vector<double> weights = FillWeightVector(state);
  return GetNewCacheIndex(state, weights);
}

absl::Status NGramWordFstModel::Read(const ModelStorage &storage) {
  RETURN_IF_ERROR(NGramFstModel::Read(storage));
  RETURN_IF_ERROR(EstablishLexicographicOrdering());
  max_cache_size_ =
      storage.ngram_word_fst_options().max_cache_size() > model_->HiOrder()
          ? storage.ngram_word_fst_options().max_cache_size()
          : kMaxNGramCache;
  cache_accessed_ = 0;
  cache_index_.resize(fst_->NumStates(), -1);
  set_start_state(fst_->Start());

  // Creates start state and unigram caches.
  return EnsureCacheIndex(fst_->Start());
}

std::pair<int, int> NGramWordFstModel::GetBeginEndIndices(int state,
                                                          int prefix_length,
                                                          int utf8_sym) {
  const std::pair<int, int> default_pair(-1, -1);  // Default pair if no range.
  StatusOr<int> orig_begin_index =
      ngram_implicit_states_->symbol_begin_index(state);
  StatusOr<int> final_index = ngram_implicit_states_->symbol_end_index(state);
  if (!orig_begin_index.ok() || !final_index.ok()) {
    return default_pair;
  }
  const SymbolTable *syms = fst().InputSymbols();
  int begin_index = *orig_begin_index;
  int begin_char = -1;
  while (begin_index <= *final_index && utf8_sym != begin_char) {
    if (!DecodeSingleUnicodeChar(impl::GetNextChar(syms->Find(
            lexicographic_order_[begin_index]), prefix_length), &begin_char)) {
      return default_pair;
    }
    if (begin_char != utf8_sym) {
      ++begin_index;
      while (begin_index <= *final_index &&
             previous_common_prefix_length_[begin_index] > prefix_length) {
        // Moves past elements with the same character in the same position.
        ++begin_index;
      }
    }
  }
  if (*final_index < begin_index) {
    // Character not found at this position within this range.
    return default_pair;
  }
  int end_index = begin_index;
  while (end_index < *final_index &&
         previous_common_prefix_length_[end_index + 1] > prefix_length) {
    // Moves to next as long as next symbol has same character in same position.
    ++end_index;
  }
  return std::make_pair(begin_index, end_index);
}

int NGramWordFstModel::NextCompleteState(int state, int model_state,
                                         int prefix_length) const {
  // Check for complete word, move to next state; otherwise unigram.
  const SymbolTable *syms = fst().InputSymbols();
  StatusOr<int> symbol_begin_index =
      ngram_implicit_states_->symbol_begin_index(state);
  if (symbol_begin_index.ok()) {
    // Due to lexicographic sort, the first position should be complete.
    const int sym = lexicographic_order_[*symbol_begin_index];
    const std::vector<std::string> this_string = StrSplitByChar(
        syms->Find(sym));
    if (this_string.size() == prefix_length) {
      // This item is complete at this prefix length.
      return NextModelState(model_state, sym);
    }
  }
  return model_->UnigramState();
}

int NGramWordFstModel::NextFirstLetterState(int state, int utf8_sym) {
  // Check for begin/end indices of first letter from pre-computed.
  const std::string u_char = EncodeUnicodeChar(utf8_sym);
  int begin_index = first_char_begin_index_;
  int end_index = -1;
  for (int i = 0; i < first_chars_.size(); ++i) {
    if (first_chars_[i] == u_char) {
      end_index = first_char_ends_[i];
      break;
    } else {
      begin_index = first_char_ends_[i] + 1;
    }
  }
  int next_state = model_->UnigramState();
  if (end_index >= 0) {
    StatusOr<int> new_state =
        ngram_implicit_states_->GetState(state, 1, begin_index, end_index);
    if (new_state.ok()) {
      next_state = *new_state;
    }
  }
  return next_state;
}

int NGramWordFstModel::NextState(int state, int utf8_sym) {
  if (state < fst().NumStates()) {
    // First letter, so using the pre-compiled end indices.
    return NextFirstLetterState(state, utf8_sym);
  }
  if (state == oov_state_ && utf8_sym == 32) {
    // TODO: introduce better method for detecting word boundary.
    return model_->UnigramState();
  }
  int next_state = oov_state_;  // Default state if OOV or error.
  StatusOr<int> model_state = ngram_implicit_states_->model_state(state);
  StatusOr<int> prefix_length = ngram_implicit_states_->prefix_length(state);
  if (model_state.ok() && *model_state >= 0 && prefix_length.ok()) {
    if (utf8_sym == 32) {
      // TODO: introduce better method for detecting word boundary.
      return NextCompleteState(state, *model_state, *prefix_length);
    }
    // Check for prefix match. extend to new implicit state.
    auto begin_end_indices =
        GetBeginEndIndices(state, *prefix_length, utf8_sym);
    if (begin_end_indices.first >= 0 && begin_end_indices.second >= 0) {
      StatusOr<int> new_state = ngram_implicit_states_->GetState(
          *model_state, *prefix_length + 1, begin_end_indices.first,
          begin_end_indices.second);
      if (new_state.ok()) {
        next_state = *new_state;
      }
    }
  }
  return next_state;
}

const std::vector<int> NGramWordFstModel::GetNextCharEnds(
    int state, int prefix_length, int begin_index, int end_index,
    std::vector<std::string> *next_chars) {
  const SymbolTable *syms = fst().InputSymbols();
  int idx = begin_index;
  std::vector<int> next_char_ends;
  next_chars->clear();
  next_chars->push_back(impl::GetNextChar(
      syms->Find(lexicographic_order_[idx++]), prefix_length));
  while (idx <= end_index) {
    while (idx <= end_index &&
           previous_common_prefix_length_[idx] > prefix_length) {
      // Moves past elements with the same character in the same position.
      ++idx;
    }
    if (idx <= end_index) {
      next_char_ends.push_back(idx - 1);
      next_chars->push_back(impl::GetNextChar(
          syms->Find(lexicographic_order_[idx++]), prefix_length));
    }
  }
  next_char_ends.push_back(end_index);
  return next_char_ends;
}

const std::vector<int> NGramWordFstModel::GetNextCharEnds(
    int state, std::vector<std::string> *next_chars) {
  if (state < fst().NumStates()) {
    // Returns first characters and their end points for word initial positions.
    *next_chars = first_chars_;
    return first_char_ends_;
  }
  StatusOr<int> prefix_length = ngram_implicit_states_->prefix_length(state);
  StatusOr<int> begin_index = ngram_implicit_states_->symbol_begin_index(state);
  StatusOr<int> end_index = ngram_implicit_states_->symbol_end_index(state);
  if (!prefix_length.ok() || !begin_index.ok() || *begin_index < 0 ||
      !end_index.ok()) {
    // Returns empty vectors for states not successfully returning these values
    // or for begin_index < 0, which is associated with the oov_state_.
    next_chars->clear();
    return std::vector<int>();
  }
  return GetNextCharEnds(state, *prefix_length, *begin_index, *end_index,
                         next_chars);
}

double NGramWordFstModel::GetRangeCost(int model_state, int begin_index,
                                       int end_index) {
  if (model_state < 0 || model_state > fst().NumStates() || begin_index < 1 ||
      begin_index > end_index || end_index >= lexicographic_order_.size() ||
      EnsureCacheIndex(model_state) != absl::OkStatus()) {
    return StdArc::Weight::Zero().Value();
  }
  const NGramStateCache &state_cache = *state_cache_[cache_index_[model_state]];
  return impl::SafeNegLogDiff(
      state_cache.cummulative_neg_log_prob(end_index),
      state_cache.cummulative_neg_log_prob(begin_index - 1));
}

double NGramWordFstModel::GetFinalCost(int model_state) {
  if (model_state < 0 || model_state > fst().NumStates() ||
      EnsureCacheIndex(model_state) != absl::OkStatus()) {
    return StdArc::Weight::Zero().Value();
  }
  return state_cache_[cache_index_[model_state]]->cummulative_neg_log_prob(0);
}

double NGramWordFstModel::GetBackedoffFinalCost(int state) {
  if (state < 0 || state > fst().NumStates()) {
    return StdArc::Weight::Zero().Value();
  }
  double cost = 0.0;
  while (state >= 0) {
    if (fst().Final(state) != StdArc::Weight::Zero()) {
      cost += fst().Final(state).Value();
      break;
    }
    StdArc::Weight backoff_weight;
    const int backoff_state = model_->GetBackoff(state, &backoff_weight);
    if (backoff_state < 0) {
      cost = StdArc::Weight::Zero().Value();
    } else {
      cost += backoff_weight.Value();
    }
    state = backoff_state;
  }
  return cost;
}

bool NGramWordFstModel::ExtractLMScores(int state, LMScores *response) {
  const StdArc::StateId current_state = CheckCurrentState(state);
  std::vector<std::string> next_chars;
  const std::vector<int> next_char_ends =
      GetNextCharEnds(current_state, &next_chars);
  if (next_char_ends.empty()) {
    // Nothing to do here.
    return true;
  }
  // Compute the label probability distribution for the given state.
  StatusOr<int> init_begin_index =
      ngram_implicit_states_->symbol_begin_index(current_state);
  if (!init_begin_index.ok() || *init_begin_index < 1) {
    // Minimum begin index is 1.
    return false;
  }
  StatusOr<int> model_state =
      ngram_implicit_states_->model_state(current_state);
  if (!model_state.ok() || EnsureCacheIndex(*model_state) != absl::OkStatus()) {
    // No initialized model state associated with this implicit state.
    return false;
  }
  state_cache_[cache_index_[*model_state]]->set_last_accessed(
      cache_accessed_++);
  std::vector<double> costs;
  int begin_index = *init_begin_index;
  int start_idx = 0;
  if (next_chars[start_idx] == " ") {
    // If the first symbol is whitespace, also calculate end-of-string prob.
    // This is done by determining the final cost of the next explicit model
    // state. The word-boundary probability mass is then split between
    // whitespace and end-of string and two next chars are added.
    int sym = lexicographic_order_[begin_index];
    int next_state = NextModelState(*model_state, sym);
    double final_cost = GetBackedoffFinalCost(next_state);
    if (final_cost != StdArc::Weight::Zero().Value()) {
      // Some probability mass towards ending the string after this word.
      ++start_idx;
    }
    // Reserve one extra slot for end-of-string plus whitespace if needed.
    response->mutable_symbols()->Reserve(next_char_ends.size() + start_idx);
    response->mutable_probabilities()->Reserve(next_char_ends.size() +
                                               start_idx);
    costs.reserve(next_char_ends.size() + start_idx);
    if (start_idx > 0) {
      // Requires splitting the probability mass at word boundary.
      double word_boundary_cost =
          GetRangeCost(*model_state, begin_index, next_char_ends[0]);

      // -logP(space) = -log(1-P(final)) -logP(word boundary).
      double space_cost = impl::SafeNegLogDiff(0.0, final_cost);
      costs.push_back(word_boundary_cost + space_cost);
      response->add_symbols(next_chars[0]);
      costs.push_back(word_boundary_cost + final_cost);
      response->add_symbols("");
      begin_index = next_char_ends[0] + 1;
    }
  } else {
    int to_reserve = next_char_ends.size();
    double final_cost = GetFinalCost(*model_state);
    if (*model_state == current_state &&
        final_cost != StdArc::Weight::Zero().Value()) {
      // Need to add in end-of-string probability.
      ++to_reserve;
    }
    response->mutable_symbols()->Reserve(to_reserve);
    response->mutable_probabilities()->Reserve(to_reserve);
    costs.reserve(to_reserve);
    if (to_reserve > next_char_ends.size()) {
      costs.push_back(final_cost);
      response->add_symbols("");
    }
  }
  for (int i = start_idx; i < next_char_ends.size(); ++i) {
    costs.push_back(
        GetRangeCost(*model_state, begin_index, next_char_ends[i]));
    response->add_symbols(next_chars[i]);
    begin_index = next_char_ends[i] + 1;
  }
  SoftmaxRenormalize(&costs);
  for (auto cost : costs) {
    response->add_probabilities(std::exp(-cost));
  }
  response->set_normalization(1.0);
  return true;
}

double NGramStateCache::cummulative_neg_log_prob(int idx) const {
  if (idx < 0 || idx >= cummulative_neg_log_probs_.size()) {
    return StdArc::Weight::Zero().Value();
  }
  return cummulative_neg_log_probs_[idx];
}

NGramImplicitStates::NGramImplicitStates(const StdVectorFst &fst,
                                         int first_char_begin_index,
                                         int first_char_end_index) {
  explicit_model_states_ = fst.NumStates();
  total_model_states_ = explicit_model_states_;
  const auto *syms = fst.InputSymbols();
  max_prefix_length_ = 0;
  for (const auto sym : *syms) {
    const int this_len = StrSplitByChar(sym.Symbol()).size();
    if (this_len > max_prefix_length_) {
      max_prefix_length_ = this_len;
    }
  }
  explicit_state_begin_index_ = first_char_begin_index;
  explicit_state_end_index_ = first_char_end_index;
}

StatusOr<int> NGramImplicitStates::GetPrefixIdx(int prefix_length) {
  if (prefix_length <= 0) {
    return absl::InternalError("Prefix length must be positive non-zero.");
  }
  if (prefix_length > max_prefix_length_) {
    return absl::InternalError("Requested prefix length longer than maximum.");
  }
  const int prefix_idx = prefix_length - 1;
  if (prefix_idx > prefix_length_implicit_state_map_.size()) {
    // Maximum requested prefix length should only extend by at most one.
    return absl::InternalError(
        "Longer prefix requested before shorter prefix.");
  }
  if (prefix_idx == prefix_length_implicit_state_map_.size()) {
    // This is the first prefix of this length that has been created.
    prefix_length_implicit_state_map_.push_back(
        absl::flat_hash_map<std::pair<int, int>, int>());
  }
  return prefix_idx;
}

int NGramImplicitStates::FindExistingState(int model_state, int prefix_length,
                                           int symbol_begin_index) {
  if (prefix_length == 0) {
    // Prefix length of 0 means word initial, i.e., same state as model state.
    if (model_state < 0 || model_state >= explicit_model_states_) {
      return -1;
    }
    return model_state;
  }
  StatusOr<int> prefix_idx = GetPrefixIdx(prefix_length);
  if (prefix_idx.ok()) {
    const auto key_pair = std::make_pair(model_state, symbol_begin_index);
    auto state_entry =
        prefix_length_implicit_state_map_[*prefix_idx].find(key_pair);
    if (state_entry != prefix_length_implicit_state_map_[*prefix_idx].end()) {
      // State was already created for this implicit state.
      return state_entry->second;
    }
  }
  return -1;  // State not found.
}

StatusOr<int> NGramImplicitStates::AddNewState(int model_state,
                                               int prefix_length,
                                               int symbol_begin_index,
                                               int symbol_end_index) {
  int prefix_idx;
  ASSIGN_OR_RETURN(prefix_idx, GetPrefixIdx(prefix_length));
  const int new_state = total_model_states_++;
  model_state_.push_back(model_state);
  prefix_length_.push_back(prefix_length);
  symbol_begin_index_.push_back(symbol_begin_index);
  symbol_end_index_.push_back(symbol_end_index);
  prefix_length_implicit_state_map_[prefix_idx].insert(
      {std::make_pair(model_state, symbol_begin_index), new_state});
  return new_state;
}

StatusOr<int> NGramImplicitStates::GetState(int model_state, int prefix_length,
                                            int symbol_begin_index,
                                            int symbol_end_index) {
  if (prefix_length == 0) {
    // Prefix length of 0 means word initial, i.e., same state as model state.
    if (model_state < 0 || model_state >= explicit_model_states_) {
      return absl::InternalError("Invalid model state for prefix length of 0.");
    }
    return model_state;
  }
  int existing_state =
      FindExistingState(model_state, prefix_length, symbol_begin_index);
  if (existing_state >= 0) {
    return existing_state;
  }
  return AddNewState(model_state, prefix_length, symbol_begin_index,
                     symbol_end_index);
}

StatusOr<int> NGramImplicitStates::GetImplicitIdx(int state) const {
  if (state < explicit_model_states_) {
    // State index is a state in the model.
    return -1;
  }
  if (state >= total_model_states_) {
    return absl::InternalError("State index does not exist.");
  }
  int implicit_idx = state - explicit_model_states_;
  if (implicit_idx >= model_state_.size() || implicit_idx < 0) {
    return absl::InternalError("Model states not correctly allocated.");
  }
  return implicit_idx;
}

StatusOr<int> NGramImplicitStates::model_state(int state) const {
  if (state < explicit_model_states_) {
    return state;
  }
  int implicit_idx;
  ASSIGN_OR_RETURN(implicit_idx, GetImplicitIdx(state));
  return model_state_[implicit_idx];
}

StatusOr<int> NGramImplicitStates::prefix_length(int state) const {
  if (state < explicit_model_states_) {
    // Prefix length for all explicit states in the model is 0.
    return 0;
  }
  int implicit_idx;
  ASSIGN_OR_RETURN(implicit_idx, GetImplicitIdx(state));
  return prefix_length_[implicit_idx];
}

StatusOr<int> NGramImplicitStates::symbol_begin_index(int state) const {
  if (state < explicit_model_states_) {
    return explicit_state_begin_index_;
  }
  int implicit_idx;
  ASSIGN_OR_RETURN(implicit_idx, GetImplicitIdx(state));
  return symbol_begin_index_[implicit_idx];
}

StatusOr<int> NGramImplicitStates::symbol_end_index(int state) const {
  if (state < explicit_model_states_) {
    return explicit_state_end_index_;
  }
  int implicit_idx;
  ASSIGN_OR_RETURN(implicit_idx, GetImplicitIdx(state));
  return symbol_end_index_[implicit_idx];
}

}  // namespace models
}  // namespace mozolm
