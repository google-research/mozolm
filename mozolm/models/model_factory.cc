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

#include "mozolm/models/model_factory.h"

#include <utility>

#include "mozolm/models/simple_bigram_char_model.h"
#include "mozolm/models/opengrm_ngram_char_model.h"

namespace mozolm {
namespace models {

absl::StatusOr<std::unique_ptr<LanguageModel>> MakeModel(
    const ModelConfig::ModelType &model_type, const ModelStorage &storage) {
  std::unique_ptr<LanguageModel> model;
  if (model_type == ModelConfig::SIMPLE_CHAR_BIGRAM) {
    model.reset(new SimpleBigramCharModel);
  } else if (model_type == ModelConfig::OPENGRM_CHAR_NGRAM) {
    model.reset(new OpenGrmNGramCharModel);
  } else {  // Shouldn't be here.
    return absl::UnimplementedError("Unsupported model type!");
  }
  const absl::Status status = model->Read(storage);
  if (status.ok()) {
    return std::move(model);
  } else {
    return status;
  }
}

absl::StatusOr<std::unique_ptr<LanguageModel>> MakeModel(
    const ModelConfig &config) {
  return MakeModel(config.type(), config.storage());
}

}  // namespace models
}  // namespace mozolm
