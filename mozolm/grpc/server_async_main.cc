// Copyright 2026 MozoLM Authors.
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
// Note that the server will wait for queries in terminal, Ctrl-C to quit.
// --------------
// DATADIR=mozolm/models/testdata
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
// o Using the PPM models:
//
//   TEXTFILE="${DATADIR}"/en_wiki_1Kline_sample.txt
//   bazel-bin/mozolm/grpc/server_async \
//     --server_config="address_uri:\"localhost:50051\" \
//     model_hub_config { model_config { type:PPM_AS_FST storage { \
//     model_file:\"$TEXTFILE\" ppm_options { max_order: 4 \
//     static_model: false } } } }"
//
// o Using the character n-gram FST model:
//
//   MODELFILE=${DATADIR}/gutenberg_en_char_ngram_o4_wb.fst
//   bazel-bin/mozolm/grpc/server_async \
//     --server_config="address_uri:\"localhost:50051\" \
//     model_hub_config { model_config { type:CHAR_NGRAM_FST storage { \
//     model_file:\"$MODELFILE\" } } }"
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
// o Using an equal mixture of PPM and word-based n-gram models:
//
//   WORDMOD="${DATADIR}"/en_wiki_1Kline_sample.katz_word3g.fst
//   TEXTFILE="${DATADIR}"/en_wiki_1Kline_sample.txt
//   bazel-bin/mozolm/grpc/server_async \
//     --server_config="address_uri:\"localhost:50051\" \
//     model_hub_config { mixture_type:INTERPOLATION model_config { \
//     type:PPM_AS_FST \
//     storage { model_file:\"$TEXTFILE\" ppm_options { max_order: 4 \
//     static_model: false } } }  model_config { type:WORD_NGRAM_FST \
//     storage { model_file:\"$WORDMOD\" } } }"

#include <string>

#include "google/protobuf/stubs/logging.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mozolm/grpc/server_config.pb.h"
#include "mozolm/grpc/server_helper.h"
#include "nisaba/port/file_util.h"
#include "third_party/protobuf/text_format.h"
#include "nisaba/port/status_macros.h"

ABSL_FLAG(std::string, server_config, "",
          "Contents of (`mozolm.grpc.ServerConfig`) protocol buffer in text "
          "format.");

ABSL_FLAG(std::string, server_config_file, "",
          "File containing the server configuration protocol buffer in text "
          "format. This flag overrides --server_config.");

ABSL_FLAG(int, async_pool_size, 0,
          "Number of threads for handling requests asynchronously.");

ABSL_FLAG(std::string, tls_server_key_file, "",
          "Private server key for SSL/TLS credentials.");

ABSL_FLAG(std::string, tls_server_cert_file, "",
          "Public server certificate for SSL/TLS credentials.");

ABSL_FLAG(std::string, tls_custom_ca_cert_file, "",
          "Custom certificate authority file. This is required for mutual "
          "authentication (when `tls_client_verify` is enabled) and must "
          "contain the certificate authority (CA) that signed the client "
          "certificate.");

ABSL_FLAG(bool, tls_client_verify, true,
          "Whether a valid client certificate is required.");

namespace mozolm {
namespace grpc {
namespace {

using nisaba::file::ReadBinaryFile;

// Initializes SSL/TLS configuration from command-line flags.
absl::Status InitTlsConfig(ServerAuthConfig::TlsConfig *tls_config) {
  std::string contents;
  tls_config->set_client_verify(absl::GetFlag(FLAGS_tls_client_verify));
  ASSIGN_OR_RETURN(contents, ReadBinaryFile(
      absl::GetFlag(FLAGS_tls_server_key_file)));
  tls_config->set_server_key(contents);
  ASSIGN_OR_RETURN(contents, ReadBinaryFile(
      absl::GetFlag(FLAGS_tls_server_cert_file)));
  tls_config->set_server_cert(contents);
  if (!absl::GetFlag(FLAGS_tls_custom_ca_cert_file).empty()) {
    ASSIGN_OR_RETURN(contents, ReadBinaryFile(
        absl::GetFlag(FLAGS_tls_custom_ca_cert_file)));
    tls_config->set_custom_ca_cert(contents);
  }
  return absl::OkStatus();
}

// Initializes configuration from command-line flags.
absl::Status InitConfigContents(std::string *config_contents) {
  const std::string config_file = absl::GetFlag(FLAGS_server_config_file);
  if (!config_file.empty()) {
    ASSIGN_OR_RETURN(*config_contents, ReadBinaryFile(config_file));
  } else if (!absl::GetFlag(FLAGS_server_config).empty()) {
    *config_contents = absl::GetFlag(FLAGS_server_config);
  } else {
    GOOGLE_LOG(INFO) << "Using default configuration";
  }
  return absl::OkStatus();
}

// Initializes configuration from command-line flags.
absl::Status InitConfigFromFlags(ServerConfig *config) {
  // Init the main body of configuration.
  std::string config_contents;
  RETURN_IF_ERROR(InitConfigContents(&config_contents));
  if (!google::protobuf::TextFormat::ParseFromString(config_contents, config)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to parse configuration from contents"));
  }
  if (absl::GetFlag(FLAGS_async_pool_size) > 0) {
    config->set_async_pool_size(absl::GetFlag(FLAGS_async_pool_size));
  }
  InitConfigDefaults(config);

  // Initialize credentials.
  if (config->auth().credential_type() == CREDENTIAL_TLS &&
      !absl::GetFlag(FLAGS_tls_server_key_file).empty()) {
    return InitTlsConfig(config->mutable_auth()->mutable_tls());
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
