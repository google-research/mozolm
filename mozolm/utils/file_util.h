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

// Common simple file utilities.

#ifndef MOZOLM_MOZOLM_UTILS_FILE_UTIL_H_
#define MOZOLM_MOZOLM_UTILS_FILE_UTIL_H_

#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace mozolm {
namespace file {

// Returns a path to a temporary file given its filename.
std::string TempFilePath(std::string_view filename);

// Writes temporary text file given its filename and contents. Returns its
// full path or error.
absl::StatusOr<std::string> WriteTempTextFile(std::string_view filename,
                                              std::string_view contents);

}  // namespace file
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_UTILS_FILE_UTIL_H_
