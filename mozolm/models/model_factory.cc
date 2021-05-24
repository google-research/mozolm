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

#include "mozolm/stubs/logging.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "mozolm/models/ngram_char_fst_model.h"
#include "mozolm/models/ngram_word_fst_model.h"
#include "mozolm/models/ppm_as_fst_model.h"
#include "mozolm/models/simple_bigram_char_model.h"
#include "mozolm/stubs/status_macros.h"

namespace mozolm {
namespace models {

absl::StatusOr<std::unique_ptr<LanguageModel>> MakeModel(
    const ModelConfig::ModelType &model_type, const ModelStorage &storage) {
  std::unique_ptr<LanguageModel> model;
  if (model_type == ModelConfig::SIMPLE_CHAR_BIGRAM) {
    model.reset(new SimpleBigramCharModel);
  } else if (model_type == ModelConfig::CHAR_NGRAM_FST) {
    model.reset(new NGramCharFstModel);
  } else if (model_type == ModelConfig::PPM_AS_FST) {
    model.reset(new PpmAsFstModel);
  } else if (model_type == ModelConfig::WORD_NGRAM_FST) {
    model.reset(new NGramWordFstModel);
  } else {  // Shouldn't be here.
    return absl::UnimplementedError("Unsupported model type!");
  }
  GOOGLE_LOG(INFO) << "Reading ...";
  const absl::Time before_t = absl::Now();
  RETURN_IF_ERROR(model->Read(storage));
  GOOGLE_LOG(INFO) << "Model read in " << (absl::Now() - before_t) /
      absl::Milliseconds(1) << " msec.";
  return std::move(model);
}

absl::StatusOr<std::unique_ptr<LanguageModel>> MakeModel(
    const ModelConfig &config) {
  return MakeModel(config.type(), config.storage());
}

absl::StatusOr<std::unique_ptr<LanguageModelHub>> MakeModelHub(
    const ModelHubConfig &config) {
  std::unique_ptr<LanguageModelHub> model_hub(new LanguageModelHub);
  if (config.model_config_size() == 0) {
    // No models specified, adds a single default model.
    auto model_status = models::MakeModel(ModelConfig());
    if (!model_status.ok()) return model_status.status();
    model_hub->AddModel(std::move(model_status.value()));
  } else {
    for (auto idx = 0; idx < config.model_config_size(); ++idx) {
      auto model_status = models::MakeModel(config.model_config(idx));
      if (!model_status.ok()) return model_status.status();
      model_hub->AddModel(std::move(model_status.value()));
    }
  }
  model_hub->InitializeModels(config);
  return std::move(model_hub);
}

}  // namespace models
}  // namespace mozolm
