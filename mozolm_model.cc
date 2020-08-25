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

#include "third_party/mozolm/mozolm_model.h"

#include <string>
#include <vector>

#include "mozolm/stubs/logging.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/mutex.h"

namespace mozolm {

int LanguageModel::ContextState(const std::string &context, int init_state) {
  int this_state = init_state < 0 ? start_state_ : init_state;
  if (!context.empty()) {
    // TODO(roark): put back UTF8 compliant single token split.
    const std::vector<std::string> context_utf8 = absl::StrSplit(
        context, absl::ByLength(1), absl::SkipEmpty());
    for (const auto& sym : context_utf8) {
      // TODO(roark): put back utf8 conversion. Currently just works with ASCII.
      this_state = NextState(this_state, static_cast<int>(sym[0]));
      if (this_state < 0)
        this_state = start_state_;  // if not found, go back to start state.
    }
  }
  return this_state;
}

}  // namespace mozolm
