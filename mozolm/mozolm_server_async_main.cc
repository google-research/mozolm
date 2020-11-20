// Copyright 2020 MozoLM Authors.
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

// Simple utility binary for launching server.
//
// Example usage:
// --------------
// DATADIR=mozolm/data
// VOCAB="${DATADIR}"/en_wiki_1Mline_char_bigram.rows.txt
// COUNTS="${DATADIR}"/en_wiki_1Mline_char_bigram.matrix.txt
// bazel-bin/mozolm/mozolm_server_async \
//   --client_server_config="server_port:\"localhost:50051\" \
//   credential_type:INSECURE server_config { vocab:\"$VOCAB\" \
//   counts:\"$COUNTS\" wait_for_clients:true }"
//
// Will wait for queries in terminal, Ctrl-C to quit.

#include <string>

#include "mozolm/stubs/logging.h"
#include "google/protobuf/text_format.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "mozolm/grpc_util.h"
#include "mozolm/grpc_util.pb.h"

ABSL_FLAG(std::string, client_server_config, "",
          "mozolm_grpc.ClientServerConfig in text format.");
ABSL_FLAG(bool, run_client, false,
          "Whether to run a client to query server.");
ABSL_FLAG(int, k_best, 1, "Number of best scoring to return");
ABSL_FLAG(bool, randgen, false, "Whether to randomly generate");
ABSL_FLAG(std::string, context_string, "", "context string for query");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  mozolm::grpc::ClientServerConfig grpc_config;
  if (!absl::GetFlag(FLAGS_client_server_config).empty()) {
    GOOGLE_CHECK(google::protobuf::TextFormat::ParseFromString(
        absl::GetFlag(FLAGS_client_server_config), &grpc_config));
  }
  mozolm::grpc::ClientServerConfigDefaults(&grpc_config);
  if (!mozolm::grpc::RunServer(
          grpc_config, absl::GetFlag(FLAGS_run_client),
          absl::GetFlag(FLAGS_k_best), absl::GetFlag(FLAGS_randgen),
          absl::GetFlag(FLAGS_context_string))) {
    return 0;
  }
  return 1;
}
