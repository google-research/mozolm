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

#include "mozolm/models/ppm_as_fst_model.h"

#include <cmath>

#include "google/protobuf/stubs/logging.h"
#include "fst/arcsort.h"
#include "fst/symbol-table.h"
#include "fst/vector-fst.h"
#include "ngram/ngram-count.h"
#include "ngram/ngram-model.h"
#include "absl/container/flat_hash_set.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "nisaba/port/timer.h"
#include "nisaba/port/file_util.h"
#include "nisaba/port/utf8_util.h"
#include "nisaba/port/status_macros.h"

namespace mozolm {
namespace models {

using nisaba::file::ReadLines;
using nisaba::utf8::EncodeUnicodeChar;
using nisaba::utf8::StrSplitByChar;

using fst::ArcIterator;
using fst::ILabelCompare;
using fst::Log64Weight;
using fst::MutableArcIterator;
using fst::StdArc;
using fst::StdVectorFst;
using fst::SymbolTable;
using fst::SymbolTableIterator;

namespace impl {
namespace {

// Creates initial empty FST with a start and unigram states.
void MakeEmpty(StdVectorFst *fst) {
  const int start_state = fst->AddState();
  const int unigram_state = fst->AddState();
  fst->SetStart(start_state);
  fst->SetFinal(unigram_state, 0.0);
  fst->AddArc(start_state, StdArc(0, 0, 0.0, unigram_state));
}

// Returns the backoff state for the current state if exists, otherwise -1.
int GetBackoffState(const StdVectorFst& fst, StdArc::StateId s) {
  int backoff_state = -1;
  if (s >= 0 && s < fst.NumStates()) {
    // Checks first arc leaving state. Will have label 0 if there is a backoff.
    ArcIterator<StdVectorFst> aiter(fst, s);
    const StdArc arc = aiter.Value();
    if (arc.ilabel == 0) {
      backoff_state = arc.nextstate;
    }
  }
  return backoff_state;
}

// Returns set of (non-epsilon) arc labels leaving state.
absl::flat_hash_set<int> GetArcLabelSet(const StdVectorFst& fst,
                                        StdArc::StateId s) {
  absl::flat_hash_set<int> label_set;
  for (ArcIterator<StdVectorFst> arc_iterator(fst, s); !arc_iterator.Done();
       arc_iterator.Next()) {
    const StdArc arc = arc_iterator.Value();
    if (arc.ilabel > 0) {
      label_set.insert(arc.ilabel);
    }
  }
  return label_set;
}

// Determines which states are backoff states, i.e., are backed-off to.
absl::StatusOr<std::vector<bool>> DetermineBackoffStates(
    const StdVectorFst& fst) {
  std::vector<bool> backoff_states(fst.NumStates());
  for (StdArc::StateId s = 0; s < backoff_states.size(); ++s) {
    const int backoff_state = GetBackoffState(fst, s);
    if (backoff_state >= static_cast<int>(backoff_states.size())) {
      return absl::InternalError("Backoff state index out of bounds.");
    }
    if (backoff_state >= 0 && !backoff_states[backoff_state]) {
      backoff_states[backoff_state] = true;
    }
  }
  return backoff_states;
}

// Aggregates the counts at a state and converts arc weights to -log.
absl::StatusOr<double> AggregateAndLogCounts(StdArc::StateId s,
                                             StdVectorFst* fst) {
  double sum_counts = 0.0;
  for (MutableArcIterator<StdVectorFst> arc_iterator(fst, s);
       !arc_iterator.Done(); arc_iterator.Next()) {
    StdArc arc = arc_iterator.Value();
    if (arc.ilabel > 0) {
      // Ignores the epsilon arc, which will get set later.
      if (arc.weight.Value() <= 0.0) {
        return absl::InternalError("Arc weight <= 0.0.");
      }
      sum_counts += arc.weight.Value();
      arc.weight = -std::log(arc.weight.Value());
      arc_iterator.SetValue(arc);
    }
  }
  if (fst->Final(s) != StdArc::Weight::Zero()) {
    // Also includes final count if final state, and converts to -log.
    sum_counts += fst->Final(s).Value();
    fst->SetFinal(s, -std::log(fst->Final(s).Value()));
  }
  return sum_counts;
}

// Aggregates counts to store on epsilon arc and converts to -log.
absl::Status FinalizeLowerOrderCounts(const std::vector<bool>& backoff_states,
                                      StdVectorFst* fst) {
  for (StdArc::StateId s = 0; s < backoff_states.size(); ++s) {
    if (backoff_states[s]) {
      double sum_counts;
      ASSIGN_OR_RETURN(sum_counts, AggregateAndLogCounts(s, fst));
      MutableArcIterator<StdVectorFst> arc_iterator(fst, s);
      StdArc arc = arc_iterator.Value();
      if (arc.ilabel == 0) {
        // Sets epsilon arc weight to -log(sum_counts).
        if (sum_counts <= 0.0) {
          return absl::InternalError("Sum of counts <= 0.0.");
        }
        arc.weight = -std::log(sum_counts);
        arc_iterator.SetValue(arc);
      }
    }
  }
  return absl::OkStatus();
}

// Sets all arc (and final state) weights to zero for backoff states.
// This is an initial step in 'single counting' lower orders.
void ZeroOutLowerOrderCounts(const std::vector<bool>& backoff_states,
                             StdVectorFst* fst) {
  for (StdArc::StateId s = 0; s < backoff_states.size(); ++s) {
    if (backoff_states[s]) {
      for (MutableArcIterator<StdVectorFst> arc_iterator(fst, s);
           !arc_iterator.Done(); arc_iterator.Next()) {
        StdArc arc = arc_iterator.Value();
        arc.weight = 0.0;
        arc_iterator.SetValue(arc);
      }
      if (fst->Final(s) != StdArc::Weight::Zero()) {
        // Only sets to zero if state is a final state.
        fst->SetFinal(s, 0.0);
      }
    }
  }
}

// Increments lower-order counts for symbols leaving each state.
void IncrementLowerOrderCounts(StdVectorFst* fst) {
  for (StdArc::StateId s = 0; s < fst->NumStates(); ++s) {
    const int backoff_state = GetBackoffState(*fst, s);
    if (backoff_state >= 0) {
      const absl::flat_hash_set<int> arc_labels = GetArcLabelSet(*fst, s);
      for (MutableArcIterator<StdVectorFst> arc_iterator(fst, backoff_state);
           !arc_iterator.Done(); arc_iterator.Next()) {
        StdArc arc = arc_iterator.Value();
        if (arc.ilabel > 0 && arc_labels.contains(arc.ilabel)) {
          // Increments non-epsilon arc weights by one.
          arc.weight = arc.weight.Value() + 1.0;
          arc_iterator.SetValue(arc);
        }
      }
      if (fst->Final(s) != StdArc::Weight::Zero()) {
        // Also increments final cost (corresponding to </S>), if final state.
        fst->SetFinal(backoff_state, fst->Final(backoff_state).Value() + 1.0);
      }
    }
  }
}

// Enforces 'update exclusions' from Steinruecken et al. [2015]; Moffat [1990].
// Highest order states have actual counts, lower order states count number of
// unique next-highest-order states that have that symbol leaving the state.
absl::Status CalculateUpdateExclusions(StdVectorFst *fst) {
  std::vector<bool> backoff_states;
  ASSIGN_OR_RETURN(backoff_states, DetermineBackoffStates(*fst));
  ZeroOutLowerOrderCounts(backoff_states, fst);
  IncrementLowerOrderCounts(fst);
  return FinalizeLowerOrderCounts(backoff_states, fst);
}

// Returns true if the state has no observed continuations.
bool NoObservations(const StdVectorFst& fst, StdArc::StateId s) {
  ArcIterator<StdVectorFst> aiter(fst, s);
  const StdArc arc = aiter.Value();
  if (fst.Final(s) == StdArc::Weight::Zero() && fst.NumArcs(s) == 1 &&
      arc.ilabel == 0) {
    // Only backoff arc at this state, no continuations.
    return true;
  }
  return false;
}

// Returns the backoff state for the current state if exists, otherwise -1. If
// found, increments the count on the backoff arc by 1, unless there are no
// prior observations from the state, in which case no need to increment.
int IncrementBackoffArcReturnBackoffState(StdVectorFst* fst,
                                          StdArc::StateId s) {
  const bool increment_count = !NoObservations(*fst, s);
  int backoff_state = -1;
  MutableArcIterator<StdVectorFst> arc_iterator(fst, s);
  StdArc arc = arc_iterator.Value();
  if (arc.ilabel == 0) {
    backoff_state = arc.nextstate;
    if (increment_count) {
      arc.weight = ngram::NegLogSum(arc.weight.Value(), 0.0);
      arc_iterator.SetValue(arc);
    }
  }
  return backoff_state;
}

// Returns the total count at the state.
double GetTotalStateCount(const StdVectorFst& fst, StdArc::StateId s) {
  double state_count = fst.Final(s).Value();
  for (ArcIterator<StdVectorFst> arc_iterator(fst, s); !arc_iterator.Done();
       arc_iterator.Next()) {
    const StdArc arc = arc_iterator.Value();
    if (arc.ilabel == 0) {
      // State count stored on epsilon arc, if it is there.
      state_count = arc.weight.Value();
      return state_count;
    }
    state_count = ngram::NegLogSum(state_count, arc.weight.Value());
  }
  return state_count;
}

// Calculates the probability contribution for index at current state.
absl::StatusOr<double> UpdateIndexProb(double count, double neg_log_beta,
                                       double denominator,
                                       double lower_order_prob,
                                       bool at_unigram) {
  double sym_prob;
  if (at_unigram) {
    // Unigram probabilities are via direct relative frequency estimation,
    // though Laplace (add 1) smoothing has already been applied to counts.
    sym_prob = count - denominator;
  } else {
    // Let h be the history, h' the backoff history and s be the symbol.
    // Returns -log( (c(hs) - \beta)/denominator + \gamma P(s | h') ).
    // where lower_order_prob is the pre-calculated \gamma P(s | h'), and
    // denominator is the pre-calculated c(h) + \alpha.
    if (count >= neg_log_beta) {
      return absl::InternalError("Found a count less than \beta.");
    }
    sym_prob = ngram::NegLogSum(
        lower_order_prob, ngram::NegLogDiff(count, neg_log_beta) - denominator);
  }
  return sym_prob;
}

// Converts from nats (base e) to bits (base 2).
double BitsFromNats(double nats) {
  return nats / std::log(2.0);
}

}  // namespace
}  // namespace impl

absl::Status PpmAsFstModel::AddExtraCharacters(
    const std::string& input_string) {
  const std::vector<std::string> syms = StrSplitByChar(input_string);
  const int unigram_state = impl::GetBackoffState(*fst_, fst_->Start());
  if (unigram_state < 0) {
    return absl::InternalError(
        "No unigram state found when adding extra characters.");
  }
  for (const auto& sym : syms) {
    if (fst_->InputSymbols()->Find(sym) < 0) {
      fst_->MutableInputSymbols()->AddSymbol(sym);
      fst_->MutableOutputSymbols()->AddSymbol(sym);
      syms_->AddSymbol(sym);
      const int idx = fst_->InputSymbols()->Find(sym);
      fst_->AddArc(unigram_state, StdArc(idx, idx, 0.0, unigram_state));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<int> PpmAsFstModel::CalculateStateOrder(int s) {
  if (state_orders_[s] >= 0) return state_orders_[s];
  const int backoff_state = impl::GetBackoffState(*fst_, s);
  if (backoff_state < 0) {
    return absl::InternalError(
        "No backoff state found when computing state orders.");
  }
  const auto backoff_state_order_status = CalculateStateOrder(backoff_state);
  if (!backoff_state_order_status.ok()) {
    return backoff_state_order_status.status();
  }
  state_orders_[s] = backoff_state_order_status.value() + 1;
  return state_orders_[s];
}

absl::Status PpmAsFstModel::CalculateStateOrders(bool save_state_orders) {
  state_orders_.resize(fst_->NumStates(), -1);
  state_orders_[fst_->Start()] = 1;
  const int unigram_state = impl::GetBackoffState(*fst_, fst_->Start());
  if (unigram_state < 0) {
    return absl::InternalError("Invalid unigram state: -1");
  }
  state_orders_[unigram_state] = 0;
  int max_state_order = 1;
  for (int s = 0; s < state_orders_.size(); ++s) {
    const auto this_state_order_status = CalculateStateOrder(s);
    if (!this_state_order_status.ok()) {
      return this_state_order_status.status();
    }
    if (this_state_order_status.value() > max_state_order) {
      max_state_order = this_state_order_status.value();
    }
  }
  if (!save_state_orders) state_orders_.clear();
  if (max_state_order >= max_order_) max_order_ = max_state_order + 1;
  return absl::OkStatus();
}

absl::Status PpmAsFstModel::AddPriorCounts() {
  const int unigram_state = impl::GetBackoffState(*fst_, fst_->Start());
  if (unigram_state < 0) {
    return absl::InternalError(
        "No unigram state found when adding prior counts.");
  }
  absl::flat_hash_set<int> has_unigram;
  for (MutableArcIterator<StdVectorFst> arc_iterator(fst_.get(), unigram_state);
       !arc_iterator.Done(); arc_iterator.Next()) {
    StdArc arc = arc_iterator.Value();
    has_unigram.insert(arc.ilabel);
    arc.weight = ngram::NegLogSum(arc.weight.Value(), 0.0);  // Adds 1 count.
    arc_iterator.SetValue(arc);
  }
  fst_->SetFinal(unigram_state,
                ngram::NegLogSum(fst_->Final(unigram_state).Value(), 0.0));
  bool syms_added = false;
  for (SymbolTableIterator syms_iter(*syms_); !syms_iter.Done();
       syms_iter.Next()) {
    const int64 sym = syms_iter.Value();
    if (sym > 0) {
      if (!has_unigram.contains(sym)) {
        // Adds unigram looping arc for possible characters without unigram.
        fst_->AddArc(unigram_state, StdArc(sym, sym, 0.0, unigram_state));
        if (!syms_added) syms_added = true;
      }
    }
  }
  if (syms_added) {
    ArcSort(fst_.get(), ILabelCompare<StdArc>());
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<int>> PpmAsFstModel::GetSymsVector(
    const std::string& input_string, bool add_sym) {
  const std::vector<std::string> syms = StrSplitByChar(input_string);
  std::vector<int> unicode_syms(syms.size());
  for (size_t i = 0; i < syms.size(); ++i) {
    unicode_syms[i] = syms_->Find(syms[i]);
    if (unicode_syms[i] < 0 && add_sym) {
      syms_->AddSymbol(syms[i]);
      fst_->MutableInputSymbols()->AddSymbol(syms[i]);
      fst_->MutableOutputSymbols()->AddSymbol(syms[i]);
      unicode_syms[i] = syms_->Find(syms[i]);
    }
    if (unicode_syms[i] <= 0) {
      return absl::InternalError("Symbol not in vocabulary");
    }
  }
  return unicode_syms;
}

absl::StatusOr<StdVectorFst> PpmAsFstModel::String2Fst(
    const std::string& input_string) {
  StdVectorFst fst;
  int curr_state = fst.AddState();
  fst.SetStart(curr_state);
  const auto syms_vector_status = GetSymsVector(input_string, /*add_sym=*/true);
  if (!syms_vector_status.ok()) {
    return syms_vector_status.status();
  }
  for (const auto& sym : syms_vector_status.value()) {
    int next_state = fst.AddState();
    fst.AddArc(curr_state, StdArc(sym, sym, 0.0, next_state));
    curr_state = next_state;
  }
  fst.SetFinal(curr_state, 0.0);
  return fst;
}

absl::Status PpmAsFstModel::TrainFromText(
    const std::vector<std::string>& istrings) {
  for (const auto& input_line : istrings) {
    if (!input_line.empty()) {
      const auto fst_status = String2Fst(input_line);
      if (!fst_status.ok()) {
        return fst_status.status();
      }
      const StdVectorFst& fst = fst_status.value();
      if (fst.NumStates() <= 0) {
        return absl::InternalError("Line read as empty string.");
      }
      if (!ngram_counter_->Count(fst)) {
        return absl::InternalError("Failure to count ngrams from string.");
      }
    }
  }
  ngram_counter_->GetFst(fst_.get());
  ArcSort(fst_.get(), ILabelCompare<StdArc>());
  RETURN_IF_ERROR(impl::CalculateUpdateExclusions(fst_.get()));
  RETURN_IF_ERROR(AddPriorCounts());
  fst_->SetInputSymbols(syms_.get());
  fst_->SetOutputSymbols(syms_.get());
  return absl::OkStatus();
}

absl::Status PpmAsFstModel::WriteFst(const std::string& ofile) {
  ArcSort(fst_.get(), ILabelCompare<StdArc>());
  if (!fst_->Write(ofile)) {
    return absl::InternalError(absl::StrCat("Failed to write FST to ", ofile));
  } else {
    return absl::OkStatus();
  }
}

absl::Status PpmAsFstModel::Read(const ModelStorage& storage) {
  const PpmAsFstOptions ppm_as_fst_config = storage.ppm_options();
  InitParameters(ppm_as_fst_config);
  if (!storage.model_file().empty() && ppm_as_fst_config.model_is_fst()) {
    GOOGLE_LOG(INFO) << "Reading FST model  ...";
    fst_ = absl::WrapUnique(StdVectorFst::Read(storage.model_file()));
    if (!fst_) {
      return absl::NotFoundError(absl::StrCat("Can't read FST model from ",
                                              storage.model_file()));
    }
    const auto syms = *fst_->InputSymbols();
    syms_ = absl::make_unique<SymbolTable>(syms);
  } else {
    // Train PPM from given text file if non-empty, empty FST otherwise.
    if (max_order_ <= 0) {
      return absl::InternalError("max_order_ must be at least 1.");
    }
    fst_ = absl::make_unique<StdVectorFst>();
    syms_ = absl::make_unique<SymbolTable>();
    syms_->AddSymbol("<epsilon>");
    fst_->SetInputSymbols(syms_.get());
    fst_->SetOutputSymbols(syms_.get());
    ngram_counter_ = absl::make_unique<ngram::NGramCounter<Log64Weight>>(
        /*order=*/max_order_);
    if (!storage.model_file().empty()) {
      GOOGLE_LOG(INFO) << "Initializing from training data ...";
      nisaba::Timer timer;
      std::vector<std::string> text_lines;
      ASSIGN_OR_RETURN(text_lines, ReadLines(storage.model_file()));
      if (!text_lines.empty()) RETURN_IF_ERROR(TrainFromText(text_lines));
      GOOGLE_LOG(INFO) << "Constructed in " << timer.ElapsedMillis() << " msec.";
    } else if (storage.vocabulary_file().empty()) {
      return absl::InternalError(absl::StrCat(
          "No vocabulary supplied and training text file \"",
          storage.model_file(), "\" is empty"));
    } else {
      // No training data, but vocabulary has been supplied.
      GOOGLE_LOG(INFO) << "Making empty model ...";
      impl::MakeEmpty(fst_.get());
    }
  }
  if (!storage.vocabulary_file().empty()) {
    std::vector<std::string> vocab_lines;
    ASSIGN_OR_RETURN(vocab_lines, ReadLines(storage.vocabulary_file()));
    if (vocab_lines.empty()) {
      return absl::InternalError(absl::StrCat(
          "Vocabulary file \"", storage.vocabulary_file(), "\" is empty"));
    }
    for (const auto &line : vocab_lines) {
      RETURN_IF_ERROR(AddExtraCharacters(line));
    }
    if (storage.model_file().empty()) {
      // We've initialized solely from the vocabulary.
      ArcSort(fst_.get(), ILabelCompare<StdArc>());
      RETURN_IF_ERROR(AddPriorCounts());
    }
  }
  RETURN_IF_ERROR(CalculateStateOrders(/*save_state_orders=*/!static_model_));
  if (max_cache_size_ < max_order_) {
    // To descent backoff needs at least max_order_ worth of cache.
    max_cache_size_ = max_order_ + 1;
  }
  cache_accessed_ = 0;
  cache_index_.resize(fst_->NumStates(), -1);
  set_start_state(fst_->Start());
  return absl::OkStatus();
}

void PpmAsFstModel::InitParameters(const PpmAsFstOptions& options) {
  max_order_ = options.max_order() > 0 ? options.max_order() : kMaxOrder;
  alpha_ = options.alpha();
  if (alpha_ <= 0.0 || alpha_ >= 1.0) {
    // Hyper-parameter out-of-range, setting to default.
    alpha_ = kAlpha;
  }
  beta_ = options.beta();
  if (beta_ <= 0.0 || beta_ >= 1.0) {
    // Hyper-parameter out-of-range, setting to default.
    beta_ = kBeta;
  }
  static_model_ = options.static_model();
  max_cache_size_ = options.max_cache_size() > max_order_
                    ? options.max_cache_size()
                    : kMaxCache;
  GOOGLE_LOG(INFO) << "Parameters: max order: " << max_order_ << ", alpha: " << alpha_
            << ", beta: " << beta_ << ", static_model: " << static_model_
            << ", max cache size: " << max_cache_size_;
}

int PpmAsFstModel::FindOldestLastAccessedCache() const {
  int least_accessed_cache = 0;
  int oldest_access = state_cache_[0].last_accessed();
  for (size_t i = 1; i < state_cache_.size(); ++i) {
    if (state_cache_[i].last_accessed() < oldest_access) {
      least_accessed_cache = i;
      oldest_access = state_cache_[i].last_accessed();
    }
  }
  return least_accessed_cache;
}

absl::Status PpmAsFstModel::GetNewCacheIndex(StdArc::StateId s) {
  if (state_cache_.size() < max_cache_size_) {
    cache_index_[s] = state_cache_.size();
    state_cache_.push_back(PpmStateCache(s));
  } else {
    const int index_to_update = FindOldestLastAccessedCache();
    const StdArc::StateId old_state = state_cache_[index_to_update].state();
    if (cache_index_[old_state] != index_to_update) {
      return absl::InternalError("Cache index not updated correctly.");
    }
    cache_index_[old_state] = -1;
    state_cache_[index_to_update] = PpmStateCache(s);
    cache_index_[s] = index_to_update;
  }
  return absl::OkStatus();
}

std::vector<int> PpmAsFstModel::InitCacheStates(
    StdArc::StateId s,
    StdArc::StateId backoff_state,
    const PpmStateCache& backoff_cache,
    bool arc_origin) const {
  std::vector<int> cache_states;
  if (backoff_state >= 0) {
    cache_states = arc_origin ? backoff_cache.arc_origin_states()
                              : backoff_cache.destination_states();
  } else {
    cache_states.resize(fst_->NumArcs(s) + 1);
  }
  return cache_states;
}

std::vector<double> PpmAsFstModel::InitCacheProbs(
    StdArc::StateId s,
    StdArc::StateId backoff_state,
    const PpmStateCache& backoff_cache,
    double denominator) const {
  std::vector<double> cache_probs;
  if (backoff_state >= 0) {
    cache_probs = backoff_cache.neg_log_probabilities();
    double num_continuations = fst_->NumArcs(s);
    if (fst_->Final(s) == StdArc::Weight::Zero()) {
      --num_continuations;
    }
    const double gamma =
        ngram::NegLogSum(-std::log(num_continuations) - std::log(beta_),
                         -std::log(alpha_)) - denominator;
    for (size_t i = 0; i < cache_probs.size(); ++i) {
      // Adds in gamma factor to backoff probabilities.
      cache_probs[i] += gamma;
    }
  } else {
    cache_probs.resize(fst_->NumArcs(s) + 1);
  }
  return cache_probs;
}

absl::Status PpmAsFstModel::UpdateCacheStatesAndProbs(
    StdArc::StateId s, StdArc::StateId backoff_state, double denominator,
    std::vector<int>* arc_origin_states, std::vector<int>* destination_states,
    std::vector<double>* neg_log_probabilities) {
  if (fst_->Final(s) != StdArc::Weight::Zero()) {
    // If final state, records final prob in index 0.
    (*arc_origin_states)[0] = s;
    (*destination_states)[0] = fst_->Start();
    ASSIGN_OR_RETURN(
        (*neg_log_probabilities)[0],
        impl::UpdateIndexProb(fst_->Final(s).Value(), -std::log(beta_),
                              denominator, (*neg_log_probabilities)[0],
                              backoff_state < 0));
  } else if (backoff_state < 0) {
    return absl::InternalError("Unigram state has zero final cost.");
  }
  for (ArcIterator<StdVectorFst> arc_iterator(*fst_, s); !arc_iterator.Done();
       arc_iterator.Next()) {
    StdArc arc = arc_iterator.Value();
    if (arc.ilabel > 0) {
      // Updates value for all non-epsilon arcs leaving state.
      if (arc.ilabel >= arc_origin_states->size()) {
        return absl::InternalError("Arc label out of bounds.");
      }
      (*arc_origin_states)[arc.ilabel] = s;
      (*destination_states)[arc.ilabel] = arc.nextstate;
      ASSIGN_OR_RETURN(
          (*neg_log_probabilities)[arc.ilabel],
          impl::UpdateIndexProb(arc.weight.Value(), -std::log(beta_),
                                denominator,
                                (*neg_log_probabilities)[arc.ilabel],
                                backoff_state < 0));
    }
  }
  SoftmaxRenormalize(neg_log_probabilities);
  return absl::OkStatus();
}

absl::Status PpmAsFstModel::UpdateCacheAtNonEmptyState(
    StdArc::StateId s, StdArc::StateId backoff_state,
    const PpmStateCache& backoff_cache) {
  absl::Status update_status = absl::OkStatus();
  std::vector<int> arc_origin_states =
      InitCacheStates(s, backoff_state, backoff_cache, /*arc_origin=*/true);
  std::vector<int> destination_states =
      InitCacheStates(s, backoff_state, backoff_cache, /*arc_origin=*/false);
  const double denominator =
      ngram::NegLogSum(impl::GetTotalStateCount(*fst_, s), -std::log(alpha_));
  std::vector<double> neg_log_probabilities =
      InitCacheProbs(s, backoff_state, backoff_cache, denominator);
  update_status = UpdateCacheStatesAndProbs(
      s, backoff_state, denominator, &arc_origin_states, &destination_states,
      &neg_log_probabilities);
  if (update_status == absl::OkStatus() && cache_index_[s] < 0) {
    update_status = GetNewCacheIndex(s);
  }
  if (update_status == absl::OkStatus()) {
    state_cache_[cache_index_[s]].UpdateCache(
        cache_accessed_++, arc_origin_states, destination_states,
        neg_log_probabilities, denominator);
  }
  return update_status;
}

absl::Status PpmAsFstModel::UpdateCacheAtState(StdArc::StateId s) {
  if (s < 0) {
    return absl::InternalError("Updating cache at state index < 0.");
  }
  if (s >= fst_->NumStates()) {
    return absl::InternalError("State index out of bounds");
  }
  const int backoff_state = impl::GetBackoffState(*fst_, s);
  PpmStateCache backoff_cache(-1);
  if (backoff_state >= 0) {
    ASSIGN_OR_RETURN(backoff_cache, EnsureCacheAtState(backoff_state));
  }
  if (impl::NoObservations(*fst_, s)) {
    // Only backoff arc, no continuations observed (yet). Just copies cache
    // information from backoff state.
    if (cache_index_[s] < 0) {
      absl::Status update_status = GetNewCacheIndex(s);
      if (update_status != absl::OkStatus()) return update_status;
    }
    state_cache_[cache_index_[s]].UpdateCache(cache_accessed_++, backoff_cache);
  } else {
    return UpdateCacheAtNonEmptyState(s, backoff_state, backoff_cache);
  }
  return absl::OkStatus();
}

absl::StatusOr<bool> PpmAsFstModel::NeedsNewState(
    StdArc::StateId curr_state,
    StdArc::StateId next_state) const {
  if (state_orders_[next_state] > state_orders_[curr_state]) {
    // No need to add a new state if the arc ascends in order.
    return false;
  }
  if (state_orders_[next_state] != state_orders_[curr_state]) {
    return absl::InternalError(
        "Descending order arcs not currently supported.");
  }
  if (state_orders_[next_state] + 1 >= max_order_) {
    // No need to add a new state if already at max_order limit.
    return false;
  }
  return true;
}

bool PpmAsFstModel::LowerOrderCacheUpdated(StdArc::StateId s) const {
  if (cache_index_[s] < 0) return true;
  const int last_updated = state_cache_[cache_index_[s]].last_updated();
  int backoff_state = impl::GetBackoffState(*fst_, s);
  while (backoff_state >= 0) {
    if (cache_index_[backoff_state] >= 0 &&
        state_cache_[cache_index_[backoff_state]].last_updated() >
            last_updated) {
      return true;
    }
    backoff_state = impl::GetBackoffState(*fst_, backoff_state);
  }
  return false;
}

absl::StatusOr<PpmStateCache> PpmAsFstModel::EnsureCacheAtState(
    StdArc::StateId s) {
  bool update_access = true;
  if (cache_index_[s] < 0 || LowerOrderCacheUpdated(s)) {
    const absl::Status update_status = UpdateCacheAtState(s);
    if (update_status != absl::OkStatus()) return update_status;
    update_access = false;
  }
  if (cache_index_[s] < 0) {
    return absl::InternalError("Cache index less than zero.");
  }
  if (cache_index_[s] >= static_cast<int>(state_cache_.size())) {
    return absl::InternalError("Cache index out of bounds.");
  }
  if (state_cache_[cache_index_[s]].state() != s) {
    return absl::InternalError("State not stored correctly in cache index.");
  }
  if (update_access) {
    state_cache_[cache_index_[s]].set_last_accessed(cache_accessed_++);
  }
  return state_cache_[cache_index_[s]];
}

absl::StatusOr<double> PpmAsFstModel::GetNegLogProb(StdArc::StateId s,
                                                    int sym_index) {
  PpmStateCache state_cache;
  ASSIGN_OR_RETURN(state_cache, EnsureCacheAtState(s));
  return state_cache.NegLogProbability(sym_index);
}

absl::StatusOr<double> PpmAsFstModel::GetNormalization(StdArc::StateId s) {
  PpmStateCache state_cache;
  ASSIGN_OR_RETURN(state_cache, EnsureCacheAtState(s));
  return state_cache.normalization();
}

absl::StatusOr<std::vector<double>> PpmAsFstModel::GetNegLogProbs(
    const std::vector<int>& sym_indices, bool return_bits) {
  std::vector<double> neg_log_probs(sym_indices.size());
  int curr_state = fst_->Start();
  for (size_t i = 0; i < sym_indices.size(); ++i) {
    ASSIGN_OR_RETURN(neg_log_probs[i],
                     GetNegLogProb(curr_state, sym_indices[i]));
    if (return_bits) neg_log_probs[i] = impl::BitsFromNats(neg_log_probs[i]);
    if (!static_model_) {
      const auto origin_state_status = GetArcOriginState(curr_state,
                                                         sym_indices[i]);
      if (!origin_state_status.ok()) {
        return origin_state_status.status();
      }
      const auto update_status =
          UpdateModel(curr_state, origin_state_status.value(), sym_indices[i]);
      if (!update_status.ok()) return update_status.status();
    }
    const auto dest_state_status = GetDestinationState(curr_state,
                                                       sym_indices[i]);
    if (!dest_state_status.ok()) {
      return dest_state_status.status();
    }
    curr_state = dest_state_status.value();
  }
  return neg_log_probs;
}

absl::StatusOr<int> PpmAsFstModel::GetArcOriginState(StdArc::StateId s,
                                                     int sym_index) {
  PpmStateCache state_cache;
  ASSIGN_OR_RETURN(state_cache, EnsureCacheAtState(s));
  return state_cache.ArcOriginState(sym_index);
}

absl::StatusOr<int> PpmAsFstModel::GetDestinationState(StdArc::StateId s,
                                                       int sym_index) {
  PpmStateCache state_cache;
  ASSIGN_OR_RETURN(state_cache, EnsureCacheAtState(s));
  return state_cache.DestinationState(sym_index);
}

absl::StatusOr<int> PpmAsFstModel::AddNewState(
    StdArc::StateId backoff_dest_state) {
  int new_state_index = fst_->AddState();
  if (new_state_index != state_orders_.size()) {
    return absl::InternalError("State indices not dense.");
  }
  state_orders_.push_back(
      backoff_dest_state >= 0 ? state_orders_[backoff_dest_state] + 1 : 0);
  cache_index_.push_back(-1);
  if (backoff_dest_state >= 0) {
    fst_->AddArc(new_state_index, StdArc(0, 0, 0.0, backoff_dest_state));
  }
  return new_state_index;
}

absl::Status PpmAsFstModel::UpdateHighestFoundState(StdArc::StateId curr_state,
                                                    int sym_index) {
  if (sym_index == 0) {
    // Adds one to final cost and sets destination state to start state.
    fst_->SetFinal(curr_state,
                  ngram::NegLogSum(fst_->Final(curr_state).Value(), 0.0));
  } else {
    // Arc with sym_index found at current state.
    int new_next_state = -1;
    int old_next_state = -1;
    for (MutableArcIterator<StdVectorFst> arc_iterator(fst_.get(), curr_state);
         !arc_iterator.Done(); arc_iterator.Next()) {
      // Scans through arcs to find arc with correct label.
      StdArc arc = arc_iterator.Value();
      if (arc.ilabel == sym_index) {
        // Determines if new destination state required and increments count.
        old_next_state = arc.nextstate;
        const auto needs_new_state_status = NeedsNewState(curr_state,
                                                          arc.nextstate);
        if (!needs_new_state_status.ok()) {
          return needs_new_state_status.status();
        }
        if (needs_new_state_status.value()) {
          new_next_state = state_orders_.size();
          arc.nextstate = new_next_state;
        }
        arc.weight = ngram::NegLogSum(arc.weight.Value(), 0.0);
        arc_iterator.SetValue(arc);
        break;
      }
    }
    if (old_next_state < 0) {
      return absl::InternalError("Existing next state value not set.");
    }
    if (new_next_state >= 0) {
      // Adds required new destination state.
      const auto add_new_state_status = AddNewState(old_next_state);
      if (!add_new_state_status.ok()) {
        return add_new_state_status.status();
      }
    }
  }
  return absl::OkStatus();
}

absl::Status PpmAsFstModel::UpdateNotFoundState(
    StdArc::StateId curr_state, StdArc::StateId highest_found_state,
    StdArc::StateId backoff_state, int sym_index) {
  const auto update_status =
      UpdateModel(backoff_state, highest_found_state, sym_index);
  if (!update_status.ok()) return update_status.status();
  int backoff_dest_state = update_status.value();
  if (sym_index == 0) {
    fst_->SetFinal(curr_state, 0.0);
  } else {
    // No arc with sym_index found at current state.
    const auto needs_new_state_status = NeedsNewState(curr_state,
                                                      backoff_dest_state);
    if (!needs_new_state_status.ok()) {
      return needs_new_state_status.status();
    }
    int dest_state = backoff_dest_state;
    if (needs_new_state_status.value()) {
      const auto add_new_state_status = AddNewState(backoff_dest_state);
      if (!add_new_state_status.ok()) {
        return add_new_state_status.status();
      }
      dest_state = add_new_state_status.value();
    }
    fst_->AddArc(curr_state, StdArc(sym_index, sym_index, 0.0, dest_state));
  }
  return absl::OkStatus();
}

absl::StatusOr<StdArc::StateId> PpmAsFstModel::UpdateModel(
    StdArc::StateId curr_state, StdArc::StateId highest_found_state,
    int sym_index) {
  absl::Status update_status;
  const int backoff_state =
      impl::IncrementBackoffArcReturnBackoffState(fst_.get(), curr_state);
  if (highest_found_state == curr_state) {
    update_status = UpdateHighestFoundState(curr_state, sym_index);
  } else {
    update_status = UpdateNotFoundState(curr_state, highest_found_state,
                                        backoff_state, sym_index);
  }
  if (update_status == absl::OkStatus()) {
    update_status = UpdateCacheAtState(curr_state);
    if (update_status == absl::OkStatus()) {
      return GetDestinationState(curr_state, sym_index);
    }
  }
  return update_status;
}

int PpmAsFstModel::NextState(int state, int utf8_sym) {
  const std::string sym = EncodeUnicodeChar(utf8_sym);
  const int sym_index = fst_->InputSymbols()->Find(sym);
  if (sym_index > 0) {
    const auto dest_state_status = GetDestinationState(state, sym_index);
    if (dest_state_status.ok()) {
      return dest_state_status.value();
    }
  }
  // If symbol is epsilon or not in vocabulary, or destination state retrieval
  // fails, next state is unigram state (no context).
  return impl::GetBackoffState(*fst_, fst_->Start());
}

bool PpmAsFstModel::ExtractLMScores(int state, LMScores* response) {
  const auto ensure_status = EnsureCacheAtState(state);
  if (!ensure_status.ok()) return false;
  const PpmStateCache state_cache = ensure_status.value();
  return state_cache.FillLMScores(*fst_->InputSymbols(), response);
}

bool PpmAsFstModel::UpdateLMCounts(int32 state,
                                   const std::vector<int>& utf8_syms,
                                   int64 count) {
  // TODO: needs Mutex locks for model updating.
  if (static_model_ || count <= 0) {
    // Returns true, nothing to update.
    return true;
  }
  for (auto utf8_sym : utf8_syms) {
    int sym_index = utf8_sym;
    if (utf8_sym > 0) {
      const std::string sym = EncodeUnicodeChar(utf8_sym);
      sym_index = fst_->InputSymbols()->Find(sym);
    }
    if (sym_index < 0) {
      // Symbol not in model, ignoring and moves to start state.
      // TODO: Possible to add symbol not covered in model?
      state = start_state();
    } else {
      const auto origin_state_status = GetArcOriginState(state, sym_index);
      if (!origin_state_status.ok()) return false;
      auto update_status =
          UpdateModel(state, origin_state_status.value(), sym_index);
      if (!update_status.ok()) return false;
      for (int c = 1; c < count; c++) {
        // Any subsequent observations accrue only at state.
        update_status = UpdateModel(state, state, sym_index);
        if (update_status.ok()) return false;
      }
      state = NextState(state, utf8_sym);
    }
  }
  return true;
}

void PpmStateCache::UpdateCache(int access_counter,
                                const PpmStateCache& state_cache) {
  last_accessed_ = access_counter;
  last_updated_ = access_counter;
  arc_origin_states_ = state_cache.arc_origin_states_;
  destination_states_ = state_cache.destination_states_;
  neg_log_probabilities_ = state_cache.neg_log_probabilities_;
  normalization_ = state_cache.normalization_;
}

void PpmStateCache::UpdateCache(
    int access_counter, const std::vector<int>& arc_origin_states,
    const std::vector<int>& destination_states,
    const std::vector<double>& neg_log_probabilities, double normalization) {
  last_accessed_ = access_counter;
  last_updated_ = access_counter;
  arc_origin_states_ = arc_origin_states;
  destination_states_ = destination_states;
  neg_log_probabilities_ = neg_log_probabilities;
  normalization_ = normalization;
}

absl::Status PpmStateCache::VerifyAccess(int sym_index,
                                         size_t vector_size) const {
  absl::Status return_status = absl::OkStatus();
  if (sym_index < 0) {
    return_status =
        absl::InternalError("Cannot access cache for sym_index < 0.");
  } else if (sym_index >= vector_size) {
    return_status =
        absl::InternalError("Cannot access cache for sym_index out of scope.");
  }
  return return_status;
}

absl::StatusOr<int> PpmStateCache::ArcOriginState(int sym_index) const {
  const absl::Status verify_status =
      VerifyAccess(sym_index, arc_origin_states_.size());
  if (verify_status == absl::OkStatus()) {
    return arc_origin_states_[sym_index];
  } else {
    return verify_status;
  }
}

absl::StatusOr<int> PpmStateCache::DestinationState(int sym_index) const {
  const absl::Status verify_status =
      VerifyAccess(sym_index, destination_states_.size());
  if (verify_status == absl::OkStatus()) {
    return destination_states_[sym_index];
  } else {
    return verify_status;
  }
}

absl::StatusOr<double> PpmStateCache::NegLogProbability(int sym_index) const {
  const absl::Status verify_status =
      VerifyAccess(sym_index, neg_log_probabilities_.size());
  if (verify_status == absl::OkStatus()) {
    return neg_log_probabilities_[sym_index];
  } else {
    return verify_status;
  }
}

bool PpmStateCache::FillLMScores(const SymbolTable& syms,
                                 LMScores* response) const {
  response->Clear();
  response->set_normalization(std::exp(-normalization_));
  const int num_scores = neg_log_probabilities_.size() + 1;
  response->mutable_symbols()->Reserve(num_scores);
  response->add_symbols("");  // Empty string by default end-of-string.
  response->mutable_probabilities()->Reserve(num_scores);
  response->add_probabilities(std::exp(-neg_log_probabilities_[0]));
  for (size_t i = 1; i < neg_log_probabilities_.size(); i++) {
    response->add_symbols(syms.Find(i));
    response->add_probabilities(std::exp(-neg_log_probabilities_[i]));
  }
  return true;
}

}  // namespace models
}  // namespace mozolm
