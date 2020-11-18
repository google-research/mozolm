// Copyright 2020 MozoLM Authors.
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

// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MOZOLM_MOZOLM_BIGRAM_CHAR_MODEL_H__
#define MOZOLM_MOZOLM_BIGRAM_CHAR_MODEL_H__

#include <string>
#include <vector>

#include "mozolm/stubs/integral_types.h"
#include "absl/synchronization/mutex.h"
#include "mozolm/mozolm_model.h"

namespace mozolm {

class BigramCharLanguageModel : public LanguageModel {

 public:
  explicit BigramCharLanguageModel(const std::string& in_vocab = "",
                                   const std::string& in_counts = "");

  // Provides the state associated with the symbol.
  int SymState(int utf8_sym);

  // Provides the symbol associated with the state.
  int StateSym(int state);

  // Provides the state reached from state following utf8_sym.
  int NextState(int state, int utf8_sym) override;

  // Copies the counts and normalization from the given state into the response.
  bool ExtractLMScores(int state, LMScores* response)
      ABSL_LOCKS_EXCLUDED(normalizer_lock_, counts_lock_) override;

  // Updates the count for the utf8_sym and normalization at the current state.
  bool UpdateLMCounts(int32 state, int32 utf8_sym, int64 count)
      ABSL_LOCKS_EXCLUDED(normalizer_lock_, counts_lock_) override;

 private:
  std::vector<int32> utf8_indices_;   // utf8 symbols in vocabulary.
  std::vector<int32> vocab_indices_;  // dimension is utf8 symbol, stores index.
  // stores normalization constant for each item in vocabulary.
  std::vector<int64> utf8_normalizer_ ABSL_GUARDED_BY(normalizer_lock_);
  absl::Mutex normalizer_lock_;       // protects normalizer information.
  // Stores counts for each bigram in dense square matrix.
  std::vector<std::vector<int64>> bigram_counts_ ABSL_GUARDED_BY(counts_lock_);
  absl::Mutex counts_lock_;  // protects count information.
};

}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_BIGRAM_CHAR_MODEL_H__
