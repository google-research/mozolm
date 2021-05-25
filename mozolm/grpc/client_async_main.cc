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

// Simple utility binary for querying live gRPC server.
//
// Example usage:
// --------------
// - To randomly generate strings:
//   bazel-bin/mozolm/grpc/client_async \
//     --client_config="server { address_uri:\"localhost:50051\" } \
//     request_type:RANDGEN"
//
// - To get 7-best symbols from context "Ask a q":
//   bazel-bin/mozolm/grpc/client_async \
//     --client_config="server { address_uri:\"localhost:50051\" } \
//     request_type:K_BEST_ITEMS k_best:7 context_string:\"Ask a q\""
//
// - To calculate bits-per-character for a given test corpus:
//   DATADIR=mozolm/models/testdata
//   TESTFILE="${DATADIR}"/en_wiki_100line_dev_sample.txt
//   bazel-bin/mozolm/grpc/client_async \
//     --client_config="server { address_uri:\"localhost:50051\" } \
//     request_type:BITS_PER_CHAR_CALCULATION test_corpus:\"${TESTFILE}\""

#include <string>

#include "mozolm/stubs/logging.h"
#include "google/protobuf/text_format.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mozolm/grpc/client_config.pb.h"
#include "mozolm/grpc/client_helper.h"
#include "mozolm/grpc/server_config.pb.h"
#include "mozolm/utils/file_util.h"
#include "mozolm/stubs/status_macros.h"

ABSL_FLAG(std::string, client_config, "",
          "Contents of protocol buffer `mozolm.grpc.ClientConfig` in text "
          "format.");

ABSL_FLAG(std::string, client_config_file, "",
          "File containing the client configuration protocol buffer in text "
          "format. This flag overrides --client_config.");

ABSL_FLAG(double, timeout_sec, 0.0,
          "Connection timeout for waiting for response (in seconds).");

ABSL_FLAG(std::string, tls_server_cert_file, "",
          "Public (root) certificate authority file for SSL/TLS credentials "
          "in PEM encoding.");

ABSL_FLAG(std::string, tls_target_name_override, "",
          "Target name override for SSL host name checking. This should *not* "
          "be used in production. Example: \"*.test.example.com\"");

ABSL_FLAG(std::string, tls_client_cert_file, "",
          "Client public certificate file for SSL/TLS credentials in "
          "PEM encoding.");

ABSL_FLAG(std::string, tls_client_key_file, "",
          "Client private key file for SSL/TLS credentials in PEM encoding.");

namespace mozolm {
namespace grpc {
namespace {

// Initializes configuration from command-line flags.
absl::Status InitConfigContents(std::string *config_contents) {
  const std::string config_file = absl::GetFlag(FLAGS_client_config_file);
  if (!config_file.empty()) {
    ASSIGN_OR_RETURN(*config_contents, file::ReadBinaryFile(config_file));
  } else if (!absl::GetFlag(FLAGS_client_config).empty()) {
    *config_contents = absl::GetFlag(FLAGS_client_config);
  } else {
    GOOGLE_LOG(INFO) << "Configuration not supplied. Using defaults";
  }
  return absl::OkStatus();
}

// Initializes SSL/TLS configuration from command-line flags.
absl::Status InitTlsConfig(ClientConfig *config) {
  ClientAuthConfig *cli_auth = config->mutable_auth();
  cli_auth->mutable_tls()->set_target_name_override(
      absl::GetFlag(FLAGS_tls_target_name_override));

  // Set server certificate.
  std::string contents;
  ASSIGN_OR_RETURN(contents, file::ReadBinaryFile(
      absl::GetFlag(FLAGS_tls_server_cert_file)));
  ServerAuthConfig *server_auth = config->mutable_server()->mutable_auth();
  server_auth->set_credential_type(CREDENTIAL_TLS);
  ServerAuthConfig::TlsConfig *server_tls_config =
      server_auth->mutable_tls();
  server_tls_config->set_server_cert(contents);

  // Set client certificate and key.
  if (!absl::GetFlag(FLAGS_tls_client_cert_file).empty()) {
    ASSIGN_OR_RETURN(contents, file::ReadBinaryFile(
        absl::GetFlag(FLAGS_tls_client_cert_file)));
    cli_auth->mutable_tls()->set_client_cert(contents);
  }
  if (!absl::GetFlag(FLAGS_tls_client_key_file).empty()) {
    ASSIGN_OR_RETURN(contents, file::ReadBinaryFile(
        absl::GetFlag(FLAGS_tls_client_key_file)));
    cli_auth->mutable_tls()->set_client_key(contents);
  }
  return absl::OkStatus();
}

// Initializes configuration from command-line flags.
absl::Status InitConfigFromFlags(ClientConfig *config) {
  // Init the main body of configuration.
  std::string config_contents;
  RETURN_IF_ERROR(InitConfigContents(&config_contents));
  if (!google::protobuf::TextFormat::ParseFromString(config_contents, config)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to parse client configuration from contents"));
  }
  config->set_timeout_sec(absl::GetFlag(FLAGS_timeout_sec));
  InitConfigDefaults(config);

  // Configure secure credentials.
  if (!absl::GetFlag(FLAGS_tls_server_cert_file).empty()) {
    RETURN_IF_ERROR(InitTlsConfig(config));
  }
  return absl::OkStatus();
}

}  // namespace
}  // namespace grpc
}  // namespace mozolm

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  mozolm::grpc::ClientConfig config;
  auto status = mozolm::grpc::InitConfigFromFlags(&config);
  if (!status.ok()) {
    GOOGLE_LOG(ERROR) << "Failed to initialize configuration: " << status.ToString();
    return 1;
  }
  status = mozolm::grpc::RunClient(config);
  if (!status.ok()) {
    GOOGLE_LOG(ERROR) << "Failed to run client: " << status.ToString();
    return 1;
  }
  return 0;
}
