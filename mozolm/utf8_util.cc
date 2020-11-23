// Copyright 2020 MozoLM Authors.
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

#include "mozolm/utf8_util.h"

#include <iterator>

#include "absl/strings/str_split.h"
#include "utf8/checked.h"

using utf8_iterator = ::utf8::iterator<std::string::const_iterator>;

namespace mozolm {
namespace utf8 {

std::vector<std::string> StrSplitByChar(const std::string &input) {
     const int num_codepoints = ::utf8::distance(input.begin(), input.end());
     std::vector<std::string> result;
     result.reserve(num_codepoints);

     utf8_iterator pos(input.begin(), input.begin(), input.end());
     const utf8_iterator pos_end(input.end(), input.begin(), input.end());
     while (pos != pos_end) {
       std::string codepoint;
       ::utf8::append(*pos, std::back_inserter(codepoint));
       result.push_back(codepoint);
       ++pos;
     }
     return result;
}

}  // namespace utf8
}  // namespace mozolm
