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
//   bazel-bin/mozolm/grpc/mozolm_server_async \
//     --client_server_config="server_port:\"localhost:50051\" \
//     credential_type:INSECURE server_config { model_hub_config { \
//     model_config { type:SIMPLE_CHAR_BIGRAM storage { \
//     vocabulary_file:\"$VOCAB\"  model_file:\"$COUNTS\" } } } \
//     wait_for_clients:true }"
//
//   Will wait for queries in terminal, Ctrl-C to quit.
//
// o Using the PPM models:
//
//   TEXTFILE="${DATADIR}"/en_wiki_1Kline_sample.txt
//   bazel-bin/mozolm/grpc/mozolm_server_async \
//     --client_server_config="server_port:\"localhost:50051\" \
//     credential_type:INSECURE server_config { model_hub_config { \
//     model_config { type:PPM_AS_FST storage { model_file:\"$TEXTFILE\" \
//     ppm_options { max_order: 4 static_model: false } } } } \
//     wait_for_clients:true }"
//
//   Will wait for queries in terminal, Ctrl-C to quit.
//
// o Using the character n-gram FST model:
//
//   MODELFILE=${DATADIR}/models/testdata/gutenberg_en_char_ngram_o4_wb.fst
//   bazel-bin/mozolm/grpc/mozolm_server_async \
//     --client_server_config="server_port:\"localhost:50051\" \
//     credential_type:INSECURE server_config { model_hub_config { \
//     model_config { type:CHAR_NGRAM_FST storage { model_file:\"$MODELFILE\" \
//     } } } wait_for_clients:true }" --logtostderr
//
//   Will wait for queries in terminal, Ctrl-C to quit.
//
// o Using an equal mixture of PPM and simple_char_bigram models:
//
//   VOCAB="${DATADIR}"/en_wiki_1Mline_char_bigram.rows.txt
//   COUNTS="${DATADIR}"/en_wiki_1Mline_char_bigram.matrix.txt
//   TEXTFILE="${DATADIR}"/en_wiki_1Kline_sample.txt
//   bazel-bin/mozolm/grpc/mozolm_server_async \
//     --client_server_config="server_port:\"localhost:50051\" \
//     credential_type:INSECURE server_config { model_hub_config { \
//     mixture_type:INTERPOLATION model_config { type:PPM_AS_FST \
//     storage { model_file:\"$TEXTFILE\" ppm_options { max_order: 4 \
//     static_model: false } } }  model_config { type:SIMPLE_CHAR_BIGRAM \
//     storage { vocabulary_file:\"$VOCAB\"  model_file:\"$COUNTS\" } } } \
//     wait_for_clients:true }"
//
//   Will wait for queries in terminal, Ctrl-C to quit.

#include <string>

#include "mozolm/stubs/logging.h"
#include "google/protobuf/text_format.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "mozolm/grpc/grpc_util.h"
#include "mozolm/grpc/grpc_util.pb.h"

ABSL_FLAG(std::string, client_server_config, "",
          "Configuration (`mozolm_grpc.ClientServerConfig`) protocol buffer in "
          "text format.");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  mozolm::grpc::ClientServerConfig grpc_config;
  if (!absl::GetFlag(FLAGS_client_server_config).empty()) {
    GOOGLE_CHECK(google::protobuf::TextFormat::ParseFromString(
        absl::GetFlag(FLAGS_client_server_config), &grpc_config));
  }
  mozolm::grpc::ClientServerConfigDefaults(&grpc_config);
  const auto status = mozolm::grpc::RunServer(grpc_config);
  if (!status.ok()) {
    GOOGLE_LOG(ERROR) << "Failed to run server: " << status.ToString();
    return 1;
  }
  return 0;
}
