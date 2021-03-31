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

#include "mozolm/grpc/grpc_util.h"

#include <memory>
#include <string>
#include <vector>

#include "include/grpcpp/grpcpp.h"
#include "include/grpcpp/security/server_credentials.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "mozolm/grpc/grpc_util.pb.h"
#include "mozolm/grpc/mozolm_client.h"
#include "mozolm/grpc/mozolm_server_async_impl.h"
#include "mozolm/models/model_factory.h"

ABSL_FLAG(double, mozolm_client_timeout, 10.0,
          "timeout to wait for response in seconds");

namespace mozolm {
namespace grpc {
namespace {

absl::Status RunCompletionServer(const ClientServerConfig& grpc_config,
                                 ::grpc::ServerBuilder* builder) {
  auto model_status = models::MakeModelHub(
      grpc_config.server_config().model_hub_config());
  if (!model_status.ok()) return model_status.status();
  MozoLMServerAsyncImpl mozolm_server(std::move(model_status.value()));
  mozolm_server.StartServer(grpc_config.server_port(), builder);
  return absl::OkStatus();;
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

bool RunServer(const ClientServerConfig& grpc_config) {
  std::shared_ptr<::grpc::ServerCredentials> creds;
  switch (grpc_config.credential_type()) {
    case ClientServerConfig::SSL:
      // TODO: setup SSL credentials.
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
  return RunCompletionServer(grpc_config, &builder).ok();
}

bool RunClient(const ClientServerConfig& grpc_config) {
  MozoLMClient client(grpc_config);
  bool success;
  std::string result;
  switch (grpc_config.client_config().request_type()) {
    case ClientConfig::RANDGEN:
      success =
          client.RandGen(grpc_config.client_config().context_string(), &result);
      break;
    case ClientConfig::K_BEST_ITEMS:
      success = client.OneKbestSample(
          grpc_config.client_config().k_best(),
          grpc_config.client_config().context_string(), &result);
      break;
    case ClientConfig::BITS_PER_CHAR_CALCULATION:
      success = client.CalcBitsPerCharacter(
          grpc_config.client_config().test_corpus(), &result);
      break;
    default:
      success = false;  // Unknown client request type.
  }
  if (success) {
    absl::PrintF("%s\n", result);
  }
  return success;
}

}  // namespace grpc
}  // namespace mozolm
