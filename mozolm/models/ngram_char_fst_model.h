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

// Character n-gram model in OpenFst format served by OpenGrm NGram library.

#ifndef MOZOLM_MOZOLM_MODELS_NGRAM_CHAR_FST_MODEL_H_
#define MOZOLM_MOZOLM_MODELS_NGRAM_CHAR_FST_MODEL_H_

#include <memory>
#include <vector>

#include "mozolm/stubs/integral_types.h"
#include "absl/status/status.h"
#include "ngram/ngram-model.h"
#include "mozolm/models/language_model.h"
#include "mozolm/models/model_storage.pb.h"

namespace mozolm {
namespace models {

// TODO: This class is an empty stub at the moment. It is
// provided for demonstration purposes to show how to hook up OpenGrm APIs.
class NGramCharFstModel : public LanguageModel {
 public:
  NGramCharFstModel() = default;
  ~NGramCharFstModel() override = default;

  // Reads the model from the model storage.
  absl::Status Read(const ModelStorage &storage) override;

  // Provides the state reached from state following utf8_sym.
  int NextState(int state, int utf8_sym) override;

  // Copies the probs and normalization from the given state into the response.
  bool ExtractLMScores(int state, LMScores* response) override;

  // Updates the count for the utf8_syms at the current state.
  bool UpdateLMCounts(int32 state, const std::vector<int>& utf8_syms,
                      int64 count) override;

 private:
  std::unique_ptr<ngram::NGramModel<ngram::StdArc>> model_;
};

}  // namespace models
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_MODELS_NGRAM_CHAR_FST_MODEL_H_
