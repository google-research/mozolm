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
#include "ngram/ngram-model.h"
#include "absl/status/status.h"
#include "mozolm/models/language_model.h"
#include "mozolm/models/model_storage.pb.h"

namespace mozolm {
namespace models {

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

  // Updates the count for the utf8_syms at the current state. Since this is a
  // read-only model, the updates are treated as no-op.
  bool UpdateLMCounts(int32 state, const std::vector<int>& utf8_syms,
                      int64 count) override;

  // Returns underlying FST, which must be initialized.
  const fst::StdVectorFst &fst() const { return *fst_; }

 protected:
  // Computes negative log probability for observing the supplied label in a
  // given state.
  fst::StdArc::Weight LabelCostInState(fst::StdArc::StateId state,
                                           fst::StdArc::Label label) const;

 private:
  // Performs model sanity check.
  absl::Status CheckModel() const;

  // Checks the current state which may be the initial state. Depending on the
  // model topology we may choose to start from the initial or the unigram
  // state.
  fst::StdArc::StateId CheckCurrentState(
      fst::StdArc::StateId state) const;

  // Language model represented by vector FST.
  std::unique_ptr<const fst::StdVectorFst> fst_;

  // N-Gram model helper wrapping the FST above.
  std::unique_ptr<const ngram::NGramModel<ngram::StdArc>> model_;

  // Label for the unknown symbol, if any.
  fst::StdArc::Label oov_label_ = fst::kNoSymbol;
};

}  // namespace models
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_MODELS_NGRAM_CHAR_FST_MODEL_H_
