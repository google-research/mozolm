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

#include "mozolm/models/model_factory.h"

#include <string>
#include <utility>

#include "google/protobuf/stubs/logging.h"
#include "absl/strings/str_cat.h"
#include "mozolm/models/ngram_char_fst_model.h"
#include "mozolm/models/ngram_word_fst_model.h"
#include "mozolm/models/ppm_as_fst_model.h"
#include "mozolm/models/simple_bigram_char_model.h"
#include "nisaba/port/timer.h"
#include "nisaba/port/status_macros.h"

namespace mozolm {
namespace models {

absl::StatusOr<std::unique_ptr<LanguageModel>> MakeModel(
    const ModelConfig::ModelType &model_type, const ModelStorage &storage) {
  const std::string model_type_name(ModelConfig::ModelType_Name(model_type));
  GOOGLE_LOG(INFO) << "[" << model_type_name << "] Manufacturing model ...";
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
    return absl::UnimplementedError(absl::StrCat("Unsupported model type: ",
                                                 model_type_name));
  }
  GOOGLE_LOG(INFO) << "[" << model_type_name << "] Reading ...";
  nisaba::Timer timer;
  RETURN_IF_ERROR(model->Read(storage));
  GOOGLE_LOG(INFO) << "[" << model_type_name << "] Model read in "
            << timer.ElapsedMillis() << " msec.";
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
    GOOGLE_LOG(INFO) << "No models specified, adding a single default model.";
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
  const auto status = model_hub->InitializeModels(config);
  if (!status.ok()) return status;
  return std::move(model_hub);
}

}  // namespace models
}  // namespace mozolm
