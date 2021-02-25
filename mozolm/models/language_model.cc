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

#include "mozolm/models/language_model.h"

#include <vector>

#include "mozolm/stubs/logging.h"
#include "mozolm/utils/utf8_util.h"

namespace mozolm {
namespace models {

int LanguageModel::ContextState(const std::string &context, int init_state) {
  int this_state = init_state < 0 ? start_state_ : init_state;
  if (!context.empty()) {
    const std::vector<std::string> context_utf8 = utf8::StrSplitByChar(context);
    for (const auto& sym : context_utf8) {
      char32 utf8_code;
      const int num_bytes = utf8::DecodeUnicodeChar(sym, &utf8_code);
      const bool utf8_ok = (utf8_code != utf8::kBadUTF8Char || num_bytes != 1);
      GOOGLE_CHECK(utf8_ok) << "Symbol " << sym << " has invalid UTF8 encoding!";
      this_state = NextState(this_state, static_cast<int>(utf8_code));
      if (this_state < 0)
        this_state = start_state_;  // if not found, go back to start state.
    }
  }
  return this_state;
}

}  // namespace models
}  // namespace mozolm
