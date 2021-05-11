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
//     --client_config="server { port:\"localhost:50051\" \
//     auth { credential_type:INSECURE } } request_type:RANDGEN"
//
// - To get 7-best symbols from context "Ask a q":
//   bazel-bin/mozolm/grpc/client_async \
//     --client_config="server { port:\"localhost:50051\" \
//     auth { credential_type:INSECURE } } request_type:K_BEST_ITEMS \
//     k_best:7 context_string:\"Ask a q\""
//
// - To calculate bits-per-character for a given test corpus:
//   DATADIR=mozolm/data
//   TESTFILE="${DATADIR}"/en_wiki_100line_dev_sample.txt
//   bazel-bin/mozolm/grpc/client_async \
//     --client_config="server { port:\"localhost:50051\" \
//     auth { credential_type:INSECURE } } \
//     request_type:BITS_PER_CHAR_CALCULATION test_corpus:\"${TESTFILE}\""

#include <string>

#include "google/protobuf/text_format.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "mozolm/grpc/client_config.pb.h"
#include "mozolm/grpc/client_helper.h"

ABSL_FLAG(std::string, client_config, "",
          "Protocol buffer `mozolm_grpc.ClientConfig` in text format.");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  mozolm::grpc::ClientConfig config;
  if (!absl::GetFlag(FLAGS_client_config).empty()) {
    GOOGLE_CHECK(google::protobuf::TextFormat::ParseFromString(
        absl::GetFlag(FLAGS_client_config), &config));
  }
  mozolm::grpc::InitConfigDefaults(&config);
  const auto status = mozolm::grpc::RunClient(config);
  if (!status.ok()) {
    GOOGLE_LOG(ERROR) << "Failed to run client: " << status.ToString();
    return 1;
  }
  return 0;
}
