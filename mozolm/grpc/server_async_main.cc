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

// Simple utility binary for launching gRPC server.
//
// Example usage:
// --------------
// DATADIR=mozolm/data
//
// o Using the simple_char_bigram models:
//
//   VOCAB="${DATADIR}"/en_wiki_1Mline_char_bigram.rows.txt
//   COUNTS="${DATADIR}"/en_wiki_1Mline_char_bigram.matrix.txt
//   bazel-bin/mozolm/grpc/server_async \
//     --server_config="address_uri:\"localhost:50051\" \
//     model_hub_config { model_config { type:SIMPLE_CHAR_BIGRAM storage { \
//     vocabulary_file:\"$VOCAB\"  model_file:\"$COUNTS\" } } }"
//
//   Will wait for queries in terminal, Ctrl-C to quit.
//
// o Using the PPM models:
//
//   TEXTFILE="${DATADIR}"/en_wiki_1Kline_sample.txt
//   bazel-bin/mozolm/grpc/server_async \
//     --server_config="address_uri:\"localhost:50051\" \
//     model_hub_config { model_config { type:PPM_AS_FST storage { \
//     model_file:\"$TEXTFILE\" ppm_options { max_order: 4 \
//     static_model: false } } } }"
//
//   Will wait for queries in terminal, Ctrl-C to quit.
//
// o Using the character n-gram FST model:
//
//   MODELFILE=${DATADIR}/models/testdata/gutenberg_en_char_ngram_o4_wb.fst
//   bazel-bin/mozolm/grpc/server_async \
//     --server_config="address_uri:\"localhost:50051\" \
//     model_hub_config { model_config { type:CHAR_NGRAM_FST storage { \
//     model_file:\"$MODELFILE\" } } }"
//
//   Will wait for queries in terminal, Ctrl-C to quit.
//
// o Using an equal mixture of PPM and simple_char_bigram models:
//
//   VOCAB="${DATADIR}"/en_wiki_1Mline_char_bigram.rows.txt
//   COUNTS="${DATADIR}"/en_wiki_1Mline_char_bigram.matrix.txt
//   TEXTFILE="${DATADIR}"/en_wiki_1Kline_sample.txt
//   bazel-bin/mozolm/grpc/server_async \
//     --server_config="address_uri:\"localhost:50051\" \
//     model_hub_config { mixture_type:INTERPOLATION model_config { \
//     type:PPM_AS_FST \
//     storage { model_file:\"$TEXTFILE\" ppm_options { max_order: 4 \
//     static_model: false } } }  model_config { type:SIMPLE_CHAR_BIGRAM \
//     storage { vocabulary_file:\"$VOCAB\"  model_file:\"$COUNTS\" } } }"
//
//   Will wait for queries in terminal, Ctrl-C to quit.

#include <string>

#include "mozolm/stubs/logging.h"
#include "google/protobuf/text_format.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mozolm/grpc/server_config.pb.h"
#include "mozolm/grpc/server_helper.h"
#include "mozolm/utils/file_util.h"
#include "mozolm/stubs/status_macros.h"

ABSL_FLAG(std::string, server_config, "",
          "Configuration (`mozolm.grpc.ServerConfig`) protocol buffer in "
          "text format.");

ABSL_FLAG(int, async_pool_size, 0,
          "Number of threads for handling requests asynchronously.");

ABSL_FLAG(std::string, ssl_server_key_file, "",
          "Private server key for SSL/TLS credentials.");

ABSL_FLAG(std::string, ssl_server_cert_file, "",
          "Public server certificate for SSL/TLS credentials.");

ABSL_FLAG(std::string, ssl_custom_ca_file, "",
          "Custom certificate authority file.");

ABSL_FLAG(bool, ssl_client_verify, true,
          "Whether a valid client certificate is required.");

namespace mozolm {
namespace grpc {
namespace {

// Initializes SSL/TLS configuration from command-line flags.
absl::Status InitSslConfig(ServerAuthConfig::SslConfig *ssl_config) {
  std::string contents;
  ssl_config->set_client_verify(absl::GetFlag(FLAGS_ssl_client_verify));
  ASSIGN_OR_RETURN(contents, file::ReadBinaryFile(
      absl::GetFlag(FLAGS_ssl_server_key_file)));
  ssl_config->set_server_key(contents);
  ASSIGN_OR_RETURN(contents, file::ReadBinaryFile(
      absl::GetFlag(FLAGS_ssl_server_cert_file)));
  ssl_config->set_server_cert(contents);
  if (!absl::GetFlag(FLAGS_ssl_custom_ca_file).empty()) {
    ASSIGN_OR_RETURN(contents, file::ReadBinaryFile(
        absl::GetFlag(FLAGS_ssl_custom_ca_file)));
    ssl_config->set_custom_ca(contents);
  }
  return absl::OkStatus();
}

// Initializes configuration from command-line flags.
absl::Status InitConfigFromFlags(ServerConfig *config) {
  // Init the main body of configuration.
  const std::string config_contents = absl::GetFlag(FLAGS_server_config);
  if (!config_contents.empty()) {
    if (!google::protobuf::TextFormat::ParseFromString(config_contents, config)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Failed to parse configuration from contents"));
    }
  }
  if (absl::GetFlag(FLAGS_async_pool_size) > 0) {
    config->set_async_pool_size(absl::GetFlag(FLAGS_async_pool_size));
  }
  InitConfigDefaults(config);

  // Initialize credentials.
  if (config->auth().credential_type() == CREDENTIAL_SSL &&
      !absl::GetFlag(FLAGS_ssl_server_key_file).empty()) {
    return InitSslConfig(config->mutable_auth()->mutable_ssl_config());
  }
  return absl::OkStatus();
}

}  // namespace
}  // namespace grpc
}  // namespace mozolm

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  mozolm::grpc::ServerConfig config;
  auto status = mozolm::grpc::InitConfigFromFlags(&config);
  if (!status.ok()) {
    GOOGLE_LOG(ERROR) << "Failed to initialize configuration: " << status.ToString();
    return 1;
  }
  status = mozolm::grpc::RunServer(config);
  if (!status.ok()) {
    GOOGLE_LOG(ERROR) << "Failed to run server: " << status.ToString();
    return 1;
  }
  return 0;
}
