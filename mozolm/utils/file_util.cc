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

#include "mozolm/utils/file_util.h"

#include <filesystem>
#include <fstream>

#include "absl/strings/str_cat.h"

namespace mozolm {
namespace file {

std::string TempFilePath(std::string_view filename) {
  const std::filesystem::path tmp_dir =
      std::filesystem::temp_directory_path();
  std::filesystem::path file_path = tmp_dir / filename;
  return file_path.string();
}

absl::StatusOr<std::string> WriteTempTextFile(std::string_view filename,
                                              std::string_view contents) {
  const std::string &path = TempFilePath(filename);
  std::ofstream out(path);
  if (!out) {
    return absl::PermissionDeniedError(absl::StrCat("Failed to open: ", path));
  }
  out << contents;
  if (!out.good()) {
    return absl::InternalError(absl::StrCat("Failed to write to", path));
  }
  return path;
}

}  // namespace file
}  // namespace mozolm
