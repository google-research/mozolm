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
#include "fst/vector-fst.h"
#include "absl/status/status.h"
#include "mozolm/models/ngram_fst_model.h"

namespace mozolm {
namespace models {

class NGramCharFstModel : public NGramFstModel {
 public:
  NGramCharFstModel() = default;
  ~NGramCharFstModel() override = default;

  // Provides the state reached from state following utf8_sym.
  int NextState(int state, int utf8_sym) override;

  // Copies the probs and normalization from the given state into the response.
  bool ExtractLMScores(int state, LMScores* response) override;

 protected:
  // Computes negative log probability for observing the supplied label in a
  // given state.
  fst::StdArc::Weight LabelCostInState(fst::StdArc::StateId state,
                                           fst::StdArc::Label label) const;
};

}  // namespace models
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_MODELS_NGRAM_CHAR_FST_MODEL_H_
