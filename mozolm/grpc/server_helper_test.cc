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

#include "mozolm/grpc/server_helper.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "google/protobuf/text_format.h"
#include "gmock/gmock.h"
#include "mozolm/stubs/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "mozolm/models/model_config.pb.h"
#include "mozolm/models/model_storage.pb.h"

namespace mozolm {
namespace grpc {
namespace {

// Returns full temporary path to `filename`.
std::string TempFilePath(std::string_view filename) {
  const std::filesystem::path tmp_dir =
      std::filesystem::temp_directory_path();
  std::filesystem::path file_path = tmp_dir / filename;
  return file_path.string();
}

// Writes temporary text file.
void WriteTempTextFile(std::string_view filename, std::string_view contents,
                       std::string *path) {
  *path = TempFilePath(filename);
  std::ofstream out(*path);
  EXPECT_TRUE(out) << "Failed to open " << *path;
  out << contents;
  EXPECT_TRUE(out.good());
}

TEST(ServerHelperTest, CheckConfig) {
  ServerConfig config;
  InitConfigDefaults(&config);
  EXPECT_EQ(kDefaultServerAddress, config.address_uri());
  EXPECT_TRUE(config.wait_for_clients());
}

TEST(ServerHelperTest, CheckRunServer) {
  // Prepare a dummy text file.
  std::string text_path;
  WriteTempTextFile("corpus.txt", "Hello world!", &text_path);
  EXPECT_FALSE(text_path.empty());

  // Check that the server can be initialized.
  ServerConfig config;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
        address_uri: "127.0.0.1:0"
        auth { credential_type:INSECURE }
        wait_for_clients: false
        model_hub_config {
          model_config {
            type: PPM_AS_FST
            storage {
              ppm_options { max_order: 3 static_model: false }
            }
          }
        })", &config));
  config.mutable_model_hub_config()->mutable_model_config(0)->
      mutable_storage()->set_model_file(text_path);
  EXPECT_OK(RunServer(config));

  // Tear down.
  std::filesystem::remove(text_path);
}

}  // namespace
}  // namespace grpc
}  // namespace mozolm
