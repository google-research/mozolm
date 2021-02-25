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

// UTF8 manipulation utilities.

#ifndef MOZOLM_MOZOLM_UTILS_UTF8_UTIL_H_
#define MOZOLM_MOZOLM_UTILS_UTF8_UTIL_H_

#include <string>
#include <vector>

#include "mozolm/stubs/integral_types.h"

namespace mozolm {
namespace utf8 {

// Marker representing invalid UTF-8 encoding as a single Unicode codepoint.
constexpr char32 kBadUTF8Char = 0xFFFD;

// Splits the provided input into equal-length strings consisting of one
// Unicode character (codepoint).
std::vector<std::string> StrSplitByChar(const std::string &input);

// Decodes the first Unicode codepoint value from a UTF-8 string representation
// of multiple unicode characters. Returns the number of bytes read from the
// string.  If the array does not contain valid UTF-8 encoding, stores
// `kBadUTF8Char` in the result `first_char` and returns 1.
int DecodeUnicodeChar(const std::string &input, char32 *first_char);

// Encodes single Unicode codepoint value as UTF-8.
std::string EncodeUnicodeChar(char32 input);

}  // namespace utf8
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_UTILS_UTF8_UTIL_H_
