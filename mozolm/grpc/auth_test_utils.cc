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

#include "mozolm/grpc/auth_test_utils.h"

#include <filesystem>
#include <vector>

#include "gmock/gmock.h"
#include "nisaba/port/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "nisaba/port/file_util.h"

namespace mozolm {
namespace grpc {
namespace test {

void ReadTlsCredFileContents(std::string_view filename, std::string *contents) {
  const std::filesystem::path file_path = (
      std::filesystem::current_path() /
      kTlsCredTestDir / filename).make_preferred();
  const auto read_status = nisaba::file::ReadBinaryFile(file_path.string());
  ASSERT_OK(read_status) << "Failed to read " << filename;
  *contents = read_status.value();
}

void ReadAllTlsCredentials(
    absl::flat_hash_map<std::string, std::string> *name2contents) {
  std::string contents;
  const std::vector<std::string> filenames = {
    kTlsServerPrivateKeyFile, kTlsServerPublicCertFile,
    kTlsServerCentralAuthCertFile, kTlsClientPrivateKeyFile,
    kTlsClientPublicCertFile, kTlsClientCentralAuthCertFile };
  for (const auto &filename : filenames) {
    ReadTlsCredFileContents(filename, &contents);
    name2contents->insert({filename, contents});
  }
}

}  // namespace test
}  // namespace grpc
}  // namespace mozolm
