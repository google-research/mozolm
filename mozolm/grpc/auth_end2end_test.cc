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

// Basic authentication end-to-end tests.

#include <filesystem>
#include <string>

#include "gmock/gmock.h"
#include "mozolm/stubs/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mozolm/grpc/auth_test_utils.h"
#include "mozolm/grpc/client_helper.h"
#include "mozolm/grpc/server_helper.h"
#include "mozolm/stubs/status_macros.h"

namespace mozolm {
namespace grpc {
namespace {

const char kModelsTestDir[] =
    "mozolm/models/testdata";
const char kCharFstModelFilename[] = "gutenberg_en_char_ngram_o2_kn.fst";

class AuthEnd2EndTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test::ReadAllSslCredentials(&ssl_name2contents_);
    MakeConfig(&config_);
  }

  // Given the configuration, builds and starts the server. Then builds the
  // client and attempts to speak to the server.
  absl::Status BuildAndRun(const ClientConfig &config) const {
    // Initialize and start the server.
    ServerHelper server;
    RETURN_IF_ERROR(server.Init(config_.server()));
    RETURN_IF_ERROR((server.Run(/* wait_till_terminated= */false)));

    // Initialize and start the client.
    ClientConfig current_config = config;
    InitConfigDefaults(&current_config);
    const int server_port = server.server().selected_port();
    EXPECT_LT(0, server_port) << "Invalid port: " << server_port;
    current_config.mutable_server()->set_address_uri(
        absl::StrCat("localhost:", server_port));
    ClientHelper client;
    RETURN_IF_ERROR(client.Init(current_config));

    // Send one random generation request.
    std::string result;
    RETURN_IF_ERROR(client.RandGen(/* context_string= */"", &result));
    EXPECT_FALSE(result.empty());
    return absl::OkStatus();
  }

  // Fills in server portion of the config.
  void MakeConfig(ClientConfig *config) const {
    // Initialize server part.
    ServerConfig *server_config = config->mutable_server();
    server_config->set_address_uri("localhost:0");
    server_config->set_wait_for_clients(false);
    auto *model = server_config->mutable_model_hub_config()->add_model_config();
    model->set_type(ModelConfig::CHAR_NGRAM_FST);
    model->mutable_storage()->set_model_file(
        ModelPath(kModelsTestDir, kCharFstModelFilename));

    // Initialize the client part.
    config->set_timeout_sec(1.0);
  }

  // Fills in server SSL config.
  void MakeServerSslConfig(ServerConfig *config, bool verify_clients) {
    ServerAuthConfig *auth = config->mutable_auth();
    auth->set_credential_type(CREDENTIAL_SSL);
    auth->mutable_ssl_config()->set_client_verify(verify_clients);
    auth->mutable_ssl_config()->set_server_key(
        ssl_name2contents_[test::kSslServerPrivateKeyFile]);
    auth->mutable_ssl_config()->set_server_cert(
        ssl_name2contents_[test::kSslServerPublicCertFile]);
  }

  // Returns full path to the model.
  std::string ModelPath(std::string_view model_dir,
                        std::string_view model_filename) const {
    const std::filesystem::path model_path = (
        std::filesystem::current_path() /
        model_dir / model_filename).make_preferred();
    return model_path.string();
  }

  // Mapping between the names of SSL credential files and the actual contents.
  absl::flat_hash_map<std::string, std::string> ssl_name2contents_;

  // Global configuration (this includes both client and the server).
  ClientConfig config_;
};

// Check insecure credentials.
TEST_F(AuthEnd2EndTest, CheckInsecure) {
  EXPECT_OK(BuildAndRun(config_));
}

// The certificate presented by the client is not checked by the server at all.
TEST_F(AuthEnd2EndTest, CheckSslNoClientVerification) {
  // Prepare the server credentials and run insecure client.
  MakeServerSslConfig(config_.mutable_server(), /* verify_clients= */false);
  EXPECT_FALSE(BuildAndRun(config_).ok());

  // Prepare the client credentials by setting the target name. Will use the
  // server public certificate authority from the server config.
  ClientAuthConfig *auth = config_.mutable_auth();
  auth->mutable_ssl_config()->set_target_name_override(test::kSslAltServerName);
  EXPECT_OK(BuildAndRun(config_));
}

// Server requests client certificate and enforces that the client presents a
// certificate.
TEST_F(AuthEnd2EndTest, CheckSslWithClientVerification) {
  // Prepare the server credentials and run insecure client.
  MakeServerSslConfig(config_.mutable_server(), /* verify_clients= */true);
  EXPECT_FALSE(BuildAndRun(config_).ok());

  // TODO: Complete.
}

}  // namespace
}  // namespace grpc
}  // namespace mozolm
