// Copyright 2025 MozoLM Authors.
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
#include "nisaba/port/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mozolm/grpc/auth_test_utils.h"
#include "mozolm/grpc/client_helper.h"
#include "mozolm/grpc/server_helper.h"
#include "nisaba/port/file_util.h"
#include "nisaba/port/test_utils.h"
#include "nisaba/port/status_macros.h"

namespace mozolm {
namespace grpc {
namespace {

const char kModelsTestDir[] =
    "com_google_mozolm/mozolm/models/testdata";
const char kCharFstModelFilename[] = "gutenberg_en_char_ngram_o2_kn.fst";
const char kUdsEndpointName[] = "auth_end2end_test.sock";
constexpr double kClientTimeoutSec = 1.0;
constexpr int kNumRequests = 5;

// The test fixtures are currently parametrized by the socket type (UDS/TCP).
class AuthEnd2EndTest : public ::testing::TestWithParam<bool>  {
 protected:
  void SetUp() override {
    test::ReadAllTlsCredentials(&tls_name2contents_);
  }

  void TearDown() override {
    // Clean up UDS, if configured.
    if (!uds_path_.empty() && std::filesystem::exists(uds_path_)) {
      EXPECT_TRUE(std::filesystem::remove(uds_path_));
    }
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
    if (uds_path_.empty()) {  // Not using UDS.
      const int server_port = server.server().selected_port();
      EXPECT_LT(0, server_port) << "Invalid port: " << server_port;
      current_config.mutable_server()->set_address_uri(
          absl::StrCat("localhost:", server_port));
    }
    ClientHelper client;
    RETURN_IF_ERROR(client.Init(current_config));

    // Send several random generation requests.
    std::string result, total_response;
    for (int i = 0; i < kNumRequests; ++i) {
      RETURN_IF_ERROR(client.RandGen(/* context_string= */"", &result));
      total_response += result;
    }
    EXPECT_FALSE(total_response.empty());
    return absl::OkStatus();
  }

  // Initializes core server and client configuration. Enabling `use_uds` will
  // configure the UNIX Domain socket (UDS) endpoint, otherwise regular TCP
  // sockets are used.
  void InitConfig(ClientConfig *config, bool use_uds) {
    // Initialize server part.
    ServerConfig *server_config = config->mutable_server();
    if (use_uds) {
      uds_path_ = nisaba::file::TempFilePath(kUdsEndpointName);
      server_config->set_address_uri(absl::StrCat("unix://", uds_path_));
    } else {
      server_config->set_address_uri("localhost:0");
    }
    server_config->set_wait_for_clients(false);
    auto *model = server_config->mutable_model_hub_config()->add_model_config();
    model->set_type(ModelConfig::CHAR_NGRAM_FST);
    model->mutable_storage()->set_model_file(
        nisaba::testing::TestFilePath(kModelsTestDir, kCharFstModelFilename));

    // Initialize the client part.
    config->set_timeout_sec(kClientTimeoutSec);
  }

  // Fills in server SSL config.
  void MakeServerTlsConfig(ServerConfig *config, bool verify_clients) {
    ServerAuthConfig *auth = config->mutable_auth();
    auth->set_credential_type(CREDENTIAL_TLS);
    auth->mutable_tls()->set_client_verify(verify_clients);
    auth->mutable_tls()->set_server_key(
        tls_name2contents_[test::kTlsServerPrivateKeyFile]);
    auth->mutable_tls()->set_server_cert(
        tls_name2contents_[test::kTlsServerPublicCertFile]);
  }

  // Mapping between the names of SSL credential files and the actual contents.
  absl::flat_hash_map<std::string, std::string> tls_name2contents_;

  // Global configuration (this includes both client and the server).
  ClientConfig config_;

  // UNIX Domain Socket (UDS) path.
  std::string uds_path_;
};

// Check insecure credentials.
TEST_P(AuthEnd2EndTest, CheckInsecure) {
  InitConfig(&config_, /* use_uds= */GetParam());
  EXPECT_OK(BuildAndRun(config_));
}

// The certificate presented by the client is not checked by the server at all.
TEST_P(AuthEnd2EndTest, CheckTlsNoClientVerification) {
  InitConfig(&config_, /* use_uds= */GetParam());

  // Prepare the server credentials and run insecure client.
  MakeServerTlsConfig(config_.mutable_server(), /* verify_clients= */false);
  EXPECT_FALSE(BuildAndRun(config_).ok());

  // Prepare the client credentials by setting the target name. Will use the
  // server public certificate authority from the server config.
  ClientAuthConfig *auth = config_.mutable_auth();
  auth->mutable_tls()->set_target_name_override(test::kTlsAltServerName);
  EXPECT_OK(BuildAndRun(config_));
}

// Mutual SSL/TLS verification: server requests client certificate and enforces
// that the client presents a certificate. This uses Certificate Authority (CA).
TEST_P(AuthEnd2EndTest, CheckTlsWithClientVerification) {
  InitConfig(&config_, /* use_uds= */GetParam());

  // Prepare the server credentials and run insecure client.
  MakeServerTlsConfig(config_.mutable_server(), /* verify_clients= */true);
  EXPECT_FALSE(BuildAndRun(config_).ok());

  // Check that correctly setting target name override is not enough as client
  // does not present any credentials.
  ClientAuthConfig::TlsConfig *client_tls =
      config_.mutable_auth()->mutable_tls();
  client_tls->set_target_name_override(test::kTlsAltServerName);
  EXPECT_FALSE(BuildAndRun(config_).ok());

  // Set up all the required certificates and keys. The server certificate and
  // key are already set up. Check successful handshake and run.
  ServerAuthConfig *server_auth = config_.mutable_server()->mutable_auth();
  server_auth->mutable_tls()->set_custom_ca_cert(
      tls_name2contents_[test::kTlsClientCentralAuthCertFile]);
  client_tls->set_client_cert(
      tls_name2contents_[test::kTlsClientPublicCertFile]);
  client_tls->set_client_key(
      tls_name2contents_[test::kTlsClientPrivateKeyFile]);
  EXPECT_OK(BuildAndRun(config_));
}


#if !defined(_MSC_VER)
// POSIX-compliant platforms.
INSTANTIATE_TEST_SUITE_P(
    AuthEnd2EndTestSuitePosix, AuthEnd2EndTest,
    // Use UNIX Domain Sockets (UDS) or the default TCP sockets.
    ::testing::Values(/* use_uds= */false, /* use_uds= */true));
#else
// UNIX domain sockets are not supported in older versions of Windows.
// See: https://devblogs.microsoft.com/commandline/af_unix-comes-to-windows/
INSTANTIATE_TEST_SUITE_P(
    AuthEnd2EndTestSuiteWindows, AuthEnd2EndTest,
    ::testing::Values(/* use_uds= */false));
#endif  // _MSC_VER (Windows).

}  // namespace
}  // namespace grpc
}  // namespace mozolm
