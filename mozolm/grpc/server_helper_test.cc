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
#include <string>
#include <string_view>

#include "mozolm/stubs/logging.h"
#include "google/protobuf/text_format.h"
#include "gmock/gmock.h"
#include "mozolm/stubs/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "mozolm/models/model_config.pb.h"
#include "mozolm/models/model_storage.pb.h"
#include "mozolm/utils/file_util.h"

namespace mozolm {
namespace grpc {
namespace {

class ServerHelperTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Prepare a dummy text file.
    const auto temp_text_status = file::WriteTempTextFile(
        "corpus.txt", "Hello world!");
    EXPECT_OK(temp_text_status.status());
    model_text_path_ = temp_text_status.value();
    EXPECT_FALSE(model_text_path_.empty());

    // Prepare configuration.
    EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
        address_uri: "localhost:0"
        auth { credential_type:INSECURE }
        wait_for_clients: false
        model_hub_config {
          model_config {
            type: PPM_AS_FST
            storage {
              ppm_options { max_order: 3 static_model: false }
            }
          }
        })", &config_));
    config_.mutable_model_hub_config()->mutable_model_config(0)->
        mutable_storage()->set_model_file(model_text_path_);
  }

  void TearDown() override {
    EXPECT_TRUE(std::filesystem::remove(model_text_path_));
  }

  // Server configuration.
  ServerConfig config_;

  // Data file for the model.
  std::string model_text_path_;
};

TEST_F(ServerHelperTest, CheckDefaultConfig) {
  ServerConfig config;
  InitConfigDefaults(&config);
  EXPECT_EQ(kDefaultServerAddress, config.address_uri());
  EXPECT_TRUE(config.wait_for_clients());
}

// Initializes and builds the server without starting the request loop.
TEST_F(ServerHelperTest, CheckRunServerWithoutEventLoop) {
  EXPECT_FALSE(config_.wait_for_clients());
  EXPECT_OK(RunServer(config_));
}

// Initializes, builds the server and starts the request loop. Then attempts
// to shut it down. All of the above is repeated multiple times.
TEST_F(ServerHelperTest, CheckRunServerWithEventLoop) {
  constexpr int kNumSteps = 5;
  ServerHelper server;
  for (int i = 0; i < kNumSteps; ++i) {
    GOOGLE_LOG(INFO) << "Iteration " << i;
    // Start the server and return leaving the request processing queue running.
    EXPECT_OK(server.Init(config_));
    EXPECT_LT(0, server.server().selected_port());
    EXPECT_FALSE(server.Init(config_).ok());  // Server already initialized.
    EXPECT_OK(server.Run(/* wait_till_terminated= */false));

    // Emulate some unrelated work in the main thread and attempt to shutdown
    // the server.
    absl::SleepFor(absl::Milliseconds(100));
    server.Shutdown();
  }
}

}  // namespace
}  // namespace grpc
}  // namespace mozolm
