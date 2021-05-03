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

// Miscellaneous file helper utilities.

#ifndef MOZOLM_MOZOLM_UTILS_UTIL_H_
#define MOZOLM_MOZOLM_UTILS_UTIL_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace mozolm {
namespace file {

// Returns full path of the resource that resides under runfiles directory.
absl::StatusOr<std::string> GetRunfilesResourcePath(absl::string_view path);

}  // namespace file
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_UTILS_UTIL_H_
