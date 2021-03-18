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

#include "mozolm/models/opengrm_ngram_char_model.h"

#include "mozolm/stubs/logging.h"

namespace mozolm {
namespace models {

absl::Status OpenGrmNGramCharModel::Read(const ModelStorage &storage) {
  return absl::UnimplementedError("I/O not available yet");
}

int OpenGrmNGramCharModel::NextState(int state, int utf8_sym) {
  GOOGLE_LOG(FATAL) << "Not implemented";
  return 0;
}

bool OpenGrmNGramCharModel::ExtractLMScores(int state, LMScores* response) {
  GOOGLE_LOG(FATAL) << "Not implemented";
  return false;
}

bool OpenGrmNGramCharModel::UpdateLMCounts(int32 state,
                                           const std::vector<int>& utf8_syms,
                                           int64 count) {
  GOOGLE_LOG(FATAL) << "Not implemented";
  return false;
}

}  // namespace models
}  // namespace mozolm
