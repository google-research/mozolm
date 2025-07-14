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

// N-gram model in OpenFst format served by OpenGrm NGram library.

#ifndef MOZOLM_MOZOLM_MODELS_NGRAM_FST_MODEL_H_
#define MOZOLM_MOZOLM_MODELS_NGRAM_FST_MODEL_H_

#include <memory>
#include <vector>

#include "fst/vector-fst.h"
#include "ngram/ngram-model.h"
#include "absl/status/status.h"
#include "mozolm/models/language_model.h"
#include "mozolm/models/model_storage.pb.h"

namespace mozolm {
namespace models {

class NGramFstModel : public LanguageModel {
 public:
  ~NGramFstModel() override = default;

  // Reads the model from the model storage.
  absl::Status Read(const ModelStorage &storage) override;

  // Updates the count for the utf8_syms at the current state. Since this is a
  // read-only model, the updates are treated as no-op.
  bool UpdateLMCounts(int state, const std::vector<int>& utf8_syms,
                      int64_t count) override;

  // Returns underlying FST, which must be initialized.
  const fst::StdVectorFst &fst() const { return *fst_; }

  fst::StdArc::Label oov_label() const { return oov_label_; }

 protected:
  NGramFstModel() = default;

  // Returns the next state reached by arc labeled with label from state s.
  // If the label is out-of-vocabulary, it will return the unigram state.
  fst::StdArc::StateId NextModelState(fst::StdArc::StateId current_state,
                                      fst::StdArc::Label label) const;

  // Language model represented by vector FST.
  std::unique_ptr<const fst::StdVectorFst> fst_;

  // N-Gram model helper wrapping the FST above.
  std::unique_ptr<const ngram::NGramModel<fst::StdArc>> model_;

  // Label for the unknown symbol, if any.
  fst::StdArc::Label oov_label_ = fst::kNoSymbol;

  // Checks the current state and sets it to the unigram state if less than
  // zero.
  fst::StdArc::StateId CheckCurrentState(fst::StdArc::StateId state) const;

 private:
  // Performs model sanity check.
  absl::Status CheckModel() const;
};

}  // namespace models
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_MODELS_NGRAM_FST_MODEL_H_
