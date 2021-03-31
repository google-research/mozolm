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

// Simple utility binary for querying launched server.
//
// Example usage:
// --------------
// - To randomly generate strings:
//   bazel-bin/mozolm/grpc/mozolm_client_async \
//     --client_server_config="server_port:\"localhost:50051\" \
//     credential_type:INSECURE \
//     client_config { request_type:RANDGEN }"
//
// - To get 7-best symbols from context "Ask a q":
//   bazel-bin/mozolm/grpc/mozolm_client_async \
//     --client_server_config="server_port:\"localhost:50051\" \
//     credential_type:INSECURE \
//     client_config { request_type:K_BEST_ITEMS \
//     k_best:7 context_string:\"Ask a q\" }"
//
// - To calculate bits-per-character for a given test corpus:
//   DATADIR=mozolm/data
//   TESTFILE="${DATADIR}"/en_wiki_100line_dev_sample.txt
//   bazel-bin/mozolm/grpc/mozolm_client_async \
//     --client_server_config="server_port:\"localhost:50051\" \
//     credential_type:INSECURE \
//     client_config { request_type:BITS_PER_CHAR_CALCULATION \
//     test_corpus:\"${TESTFILE}\" }"

#include <string>

#include "google/protobuf/text_format.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "mozolm/grpc/grpc_util.h"
#include "mozolm/grpc/grpc_util.pb.h"
#include "mozolm/grpc/mozolm_client.h"

ABSL_FLAG(std::string, client_server_config, "",
              "mozolm_grpc.ClientServerConfig in text format.");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  mozolm::grpc::ClientServerConfig grpc_config;
  if (!absl::GetFlag(FLAGS_client_server_config).empty()) {
    GOOGLE_CHECK(google::protobuf::TextFormat::ParseFromString(
        absl::GetFlag(FLAGS_client_server_config), &grpc_config));
  }
  mozolm::grpc::ClientServerConfigDefaults(&grpc_config);
  if (mozolm::grpc::RunClient(grpc_config)) {
    return 0;
  }

  return 1;
}
