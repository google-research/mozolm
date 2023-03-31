// Copyright 2023 MozoLM Authors.
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

#include "mozolm/models/simple_bigram_char_model.h"

#include <cmath>
#include <fstream>

#include "google/protobuf/stubs/logging.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "nisaba/port/utf8_util.h"
#include "nisaba/port/status_macros.h"

namespace mozolm {
namespace models {
namespace {

absl::StatusOr<std::vector<int32>> ReadVocabSymbols(
    const std::string& in_vocab) {
  int32 last_idx = -1;
  std::ifstream infile(in_vocab);
  if (!infile.is_open()) {
    return absl::NotFoundError(absl::StrCat("File not found: ", in_vocab));
  }
  std::string str;
  std::vector<int32> utf8_indices;
  while (std::getline(infile, str)) {
    if (str.empty()) return absl::InternalError("Empty line");
    const std::vector<std::string> str_fields =
        absl::StrSplit(str, ' ', absl::SkipEmpty());
    if (str_fields.size() != 1) {
      return absl::InternalError("Expects one column per vocab entry");
    }
    const int32 utf8_sym = std::stoi(str_fields[0]);
    if (utf8_sym <= last_idx) {
      return absl::InternalError("Assumes sorted unique numeric vocab");
    }
    utf8_indices.push_back(utf8_sym);
    last_idx = utf8_sym;
  }
  infile.close();
  return utf8_indices;
}

absl::Status ReadCountMatrix(const std::string& in_counts, int rows,
                             std::vector<double>* utf8_normalizer,
                             std::vector<std::vector<int64>>* bigram_matrix) {
  int idx = 0;
  std::ifstream infile(in_counts);
  if (!infile.is_open()) {
    return absl::NotFoundError(absl::StrCat("File not found: ", in_counts));
  }
  std::string str;
  while (std::getline(infile, str)) {
    if (str.empty()) return absl::InternalError("Empty line");
    const std::vector<std::string> str_fields =
        absl::StrSplit(str, ' ', absl::SkipEmpty());
    if (str_fields.size() != rows) {
      return absl::InternalError(absl::StrCat(
          "Expects ", rows, " columns per vocab entry"));
    }
    std::vector<int64> bigram_counts(rows, 1);
    for (size_t i = 0; i < str_fields.size(); i++) {
      int64 count = std::stol(str_fields[i]);
      if (count > 1) {
        // Counts less than one default to one, hence not updated.
        bigram_counts[i] = count;
      }
      (*utf8_normalizer)[idx] += bigram_counts[i];
    }
    bigram_matrix->push_back(bigram_counts);
    ++idx;
  }
  if (idx != rows) {
    return absl::InternalError("Expects one row per vocab entry");
  }
  infile.close();
  return absl::OkStatus();
}

}  // namespace

absl::Status SimpleBigramCharModel::Read(const ModelStorage &storage) {
  absl::ReaderMutexLock nl(&normalizer_lock_);
  absl::ReaderMutexLock cl(&counts_lock_);
  const std::string &vocab_file = storage.vocabulary_file();
  const std::string &counts_file = storage.model_file();
  if (!vocab_file.empty()) {
    ASSIGN_OR_RETURN(utf8_indices_, ReadVocabSymbols(vocab_file));
    utf8_normalizer_.resize(utf8_indices_.size(), 0);
    if (!counts_file.empty()) {
      // Only reads from bigram count file if vocab file also provided.
      RETURN_IF_ERROR(ReadCountMatrix(counts_file, utf8_indices_.size(),
                                      &utf8_normalizer_, &bigram_counts_));
    }
  } else {
    // Assumes uniform distribution over lowercase a-z and whitespace.
    GOOGLE_LOG(WARNING) << "No vocabulary and counts files specified.";
    utf8_indices_.push_back(0);   // Index 0 is <S> and </S> by convention.
    utf8_indices_.push_back(32);  // Whitespace.
    for (int32 sym = 97; sym <= 122; sym++) {
      utf8_indices_.push_back(sym);
    }
  }
  if (bigram_counts_.empty()) {
    if (utf8_normalizer_.empty()) {
      utf8_normalizer_.resize(utf8_indices_.size(), 0);
    }
    for (size_t i = 0; i < utf8_indices_.size(); i++) {
      utf8_normalizer_[i] = utf8_indices_.size();
      bigram_counts_.push_back(std::vector<int64>(utf8_indices_.size(), 1));
    }
  }
  vocab_indices_.resize(utf8_indices_.back() + 1, -1);
  for (size_t i = 0; i < utf8_indices_.size(); i++) {
    const int utf8_index = utf8_indices_[i];
    if (utf8_index >= vocab_indices_.size()) {
      return absl::OutOfRangeError(absl::StrCat(
          "Invalid UTF8 index ", utf8_index, " for vocab of size ",
          vocab_indices_.size()));
    }
    if (utf8_index < 0) {
      return absl::InternalError(absl::StrCat("Invalid UTF index", utf8_index));
    }
    vocab_indices_[utf8_index] = i;
  }
  return absl::OkStatus();
}

bool SimpleBigramCharModel::ValidState(int state) const {
  return state < static_cast<int>(utf8_indices_.size()) && state >= 0;
}

bool SimpleBigramCharModel::ValidSym(int utf8_sym) const {
  return utf8_sym < static_cast<int>(vocab_indices_.size()) && utf8_sym >= 0;
}

int SimpleBigramCharModel::StateSym(int state) {
  return ValidState(state) ? utf8_indices_[state] : -1;
}

int SimpleBigramCharModel::SymState(int utf8_sym) {
  return ValidSym(utf8_sym) ? vocab_indices_[utf8_sym] : -1;
}

int SimpleBigramCharModel::NextState(int state, int utf8_sym) {
  return SymState(utf8_sym);
}

bool SimpleBigramCharModel::ExtractLMScores(int state, LMScores* response) {
  absl::ReaderMutexLock nl(&normalizer_lock_);
  absl::ReaderMutexLock cl(&counts_lock_);
  if (!ValidState(state)) {
    // Invalid state, switching to start state, by convention state 0.
    state = 0;
  }
  response->set_normalization(utf8_normalizer_[state]);
  for (size_t i = 0; i < bigram_counts_[state].size(); i++) {
    response->add_symbols(nisaba::utf8::EncodeUnicodeChar(utf8_indices_[i]));
    response->add_probabilities(static_cast<double>(bigram_counts_[state][i]) /
                                utf8_normalizer_[state]);
  }
  return true;
}

double SimpleBigramCharModel::SymLMScore(int state, int utf8_sym) {
  if (!ValidState(state)) {
    // Invalid state, switching to start state, by convention state 0.
    state = 0;
  }
  int sym_state = NextState(state, utf8_sym);
  double prob = 0.0;
  if (ValidState(state) && ValidState(sym_state)) {
    absl::ReaderMutexLock nl(&normalizer_lock_);
    absl::ReaderMutexLock cl(&counts_lock_);
    prob = static_cast<double>(bigram_counts_[state][sym_state]) /
           utf8_normalizer_[state];
  }
  return -std::log(prob);
}

bool SimpleBigramCharModel::UpdateLMCounts(int state,
                                           const std::vector<int>& utf8_syms,
                                           int64 count) {
  absl::WriterMutexLock nl(&normalizer_lock_);
  absl::WriterMutexLock cl(&counts_lock_);
  if (count <= 0) {
    // Returns true, nothing to update.
    return true;
  }
  if (!ValidState(state)) {
    // Invalid state, switching to start state, by convention state 0.
    state = 0;
  }
  for (auto utf8_sym : utf8_syms) {
    int next_state = NextState(state, utf8_sym);
    if (next_state < 0) {
      // Symbol is not covered in model, skips count and moves to start state.
      next_state = 0;
    } else {
      utf8_normalizer_[state] += count;
      bigram_counts_[state][next_state] += count;
    }
    state = next_state;
  }
  return true;
}

}  // namespace models
}  // namespace mozolm
