// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mozolm/grpc_util.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/strings/str_format.h"
#include "include/grpcpp/grpcpp.h"
#include "include/grpcpp/security/server_credentials.h"
#include "mozolm/grpc_util.pb.h"
#include "mozolm/mozolm_client.h"
#include "mozolm/mozolm_server_async_impl.h"

ABSL_FLAG(double, mozolm_client_timeout, 10.0,
          "timeout to wait for response in seconds");

namespace mozolm {
namespace grpc {
namespace {

bool RunCompletionServer(const ClientServerConfig& grpc_config,
                         ::grpc::ServerBuilder* builder) {
  MozoLMServerAsyncImpl mozolm_server(grpc_config.server_config().vocab(),
                                      grpc_config.server_config().counts());
  mozolm_server.StartServer(grpc_config.server_port(), builder);
  return true;
}

}  // namespace

void ClientServerConfigDefaults(ClientServerConfig* config) {
  if (config->server_port().empty()) {
    config->set_server_port(kDefaultServerPort);
  }
  if (!config->has_client_config() ||
      config->client_config().timeout() <= 0.0) {
    config->mutable_client_config()->set_timeout(
        absl::GetFlag(FLAGS_mozolm_client_timeout));
  }
}

bool RunServer(const ClientServerConfig& grpc_config, bool run_client,
               int k_best, bool randgen, const std::string& context_string) {
  std::shared_ptr<::grpc::ServerCredentials> creds;
  switch (grpc_config.credential_type()) {
    case ClientServerConfig::SSL:
      // TODO(roark): setup SSL credentials.
      creds = ::grpc::InsecureServerCredentials();
      break;
    case ClientServerConfig::INSECURE:
      creds = ::grpc::InsecureServerCredentials();
      break;
    default:
      return false;  // Unknown credential type.
  }
  ::grpc::ServerBuilder builder;
  builder.AddListeningPort(grpc_config.server_port(), creds);
  return RunCompletionServer(grpc_config, &builder);
}

bool RunClient(const ClientServerConfig& grpc_config, int k_best, bool randgen,
               const std::string& context_string) {
  MozoLMClient client(grpc_config);
  bool success;
  std::string result;
  if (randgen) {
    success = client.RandGen(context_string, &result);
  } else {
    success = client.OneKbestSample(k_best, context_string, &result);
  }
  if (success) {
    absl::PrintF("%s\n", result);
  } else {
    return false;
  }
  return true;
}

}  // namespace grpc
}  // namespace mozolm
