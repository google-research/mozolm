// Copyright 2025 MozoLM Authors.
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

#ifndef MOZOLM_MOZOLM_MODELS_SIMPLE_BIGRAM_CHAR_MODEL_H_
#define MOZOLM_MOZOLM_MODELS_SIMPLE_BIGRAM_CHAR_MODEL_H_

#include <vector>

#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "mozolm/models/language_model.h"
#include "mozolm/models/model_storage.pb.h"

namespace mozolm {
namespace models {

class SimpleBigramCharModel : public LanguageModel {
 public:
  SimpleBigramCharModel() = default;
  ~SimpleBigramCharModel() override = default;

  // Reads the model from the model storage.
  absl::Status Read(const ModelStorage &storage) override;

  // Provides the symbol associated with the state.
  int StateSym(int state) override;

  // Provides the state reached from state following utf8_sym.
  int NextState(int state, int utf8_sym) override;

  // Copies the probs and normalization from the given state into the response.
  bool ExtractLMScores(int state, LMScores* response)
      ABSL_LOCKS_EXCLUDED(normalizer_lock_, counts_lock_) override;

  // Returns the negative log probability of the utf8_sym at the state.
  double SymLMScore(int state, int utf8_sym) override;

  // Updates the counts for the utf8_syms at the current state.
  bool UpdateLMCounts(int state, const std::vector<int>& utf8_syms,
                      int64_t count)
      ABSL_LOCKS_EXCLUDED(normalizer_lock_, counts_lock_) override;

  // Returns false since these models are always dynamic.
  bool IsStatic() const override { return false; }

  // Returns number of symbols in the model.
  int NumSymbols() const { return utf8_indices_.size(); }

 private:
  // Returns true if state is within the range of states in the model.
  bool ValidState(int state) const;

  // Returns true if utf8_syn is a symbol within the model.
  bool ValidSym(int utf8_sym) const;

  // Provides the state associated with the symbol.
  int SymState(int utf8_sym);

  std::vector<int> utf8_indices_;   // utf8 symbols in vocabulary.
  std::vector<int> vocab_indices_;  // dimension is utf8 symbol, stores index.
  // stores normalization constant for each item in vocabulary.
  std::vector<double> utf8_normalizer_ ABSL_GUARDED_BY(normalizer_lock_);
  absl::Mutex normalizer_lock_;       // protects normalizer information.
  // Stores counts for each bigram in dense square matrix.
  std::vector<std::vector<int64_t>> bigram_counts_
      ABSL_GUARDED_BY(counts_lock_);
  absl::Mutex counts_lock_;  // protects count information.
};

}  // namespace models
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_MODELS_SIMPLE_BIGRAM_CHAR_MODEL_H_
