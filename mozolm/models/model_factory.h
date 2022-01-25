// Copyright 2022 MozoLM Authors.
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

#ifndef MOZOLM_MOZOLM_MODELS_MODEL_FACTORY_H_
#define MOZOLM_MOZOLM_MODELS_MODEL_FACTORY_H_

#include <memory>

#include "absl/status/statusor.h"
#include "mozolm/models/language_model.h"
#include "mozolm/models/language_model_hub.h"
#include "mozolm/models/model_config.pb.h"
#include "mozolm/models/model_storage.pb.h"

namespace mozolm {
namespace models {

// Given the model configuration manufactures an initialized instance of the
// requested type.
absl::StatusOr<std::unique_ptr<LanguageModel>> MakeModel(
    const ModelConfig &config);

// Same as above, but the model type and the model storage are provided in
// separate arguments.
absl::StatusOr<std::unique_ptr<LanguageModel>> MakeModel(
    const ModelConfig::ModelType &model_type, const ModelStorage &storage);

// Given model hub configuration, initializes all model instances.
absl::StatusOr<std::unique_ptr<LanguageModelHub>> MakeModelHub(
    const ModelHubConfig &config);

}  // namespace models
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_MODELS_MODEL_FACTORY_H_
