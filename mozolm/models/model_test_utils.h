// Copyright 2023 MozoLM Authors.
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

// Collection of general model test utilities.

#ifndef MOZOLM_MOZOLM_MODELS_MODEL_TEST_UTILS_H_
#define MOZOLM_MOZOLM_MODELS_MODEL_TEST_UTILS_H_

#include <string>
#include <utility>

#include "mozolm/models/language_model.h"

namespace mozolm {
namespace models {

// Checks the top candidate returned by the `model` given the `context`. Returns
// a pair consisting of log-probability and the corresponding best candidate.
void CheckTopCandidateForContext(const std::string &context,
                                 LanguageModel *model,
                                 std::pair<double, std::string> *candidate);

}  // namespace models
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_MODELS_MODEL_TEST_UTILS_H_
