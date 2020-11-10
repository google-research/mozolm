// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MOZOLM_MOZOLM_MODEL_H__
#define MOZOLM_MOZOLM_MODEL_H__

#include <string>
#include <vector>

#include "mozolm/stubs/integral_types.h"
#include "absl/synchronization/mutex.h"
#include "mozolm/lm_scores.pb.h"

namespace mozolm {

class LanguageModel {

 public:
  explicit LanguageModel() {
    start_state_ = 0;
  }

  virtual ~LanguageModel() = default;

  // Provides the state reached from state following utf8_sym.
  virtual int NextState(int state, int utf8_sym) {
    return -1;  // Requires a derived class to complete.
  }

  // Provides the state reached from the init_state after consuming the context
  // string. If string is empty, returns the init_state.  If init_state is less
  // than zero, the model will start at the start state of the model.
  int ContextState(const std::string &context = "", int init_state = -1);

  // Copies the counts and normalization from the given state into the response.
  virtual bool ExtractLMScores(int state, LMScores* response) {
    return false;  // Requires a derived class to complete.
  }

  // Updates the count for the utf8_sym and normalization at the current state.
  virtual bool UpdateLMCounts(int32 state, int32 utf8_sym, int64 count) {
    return false;  // Requires a derived class to complete.
  }

 private:
  int32 start_state_;
};

}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_MODEL_H__
