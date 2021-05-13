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

#include "mozolm/grpc/server_helper.h"

#include <memory>

#include "include/grpcpp/grpcpp.h"
#include "include/grpcpp/security/server_credentials.h"
#include "mozolm/grpc/server_async_impl.h"
#include "mozolm/models/model_factory.h"

namespace mozolm {
namespace grpc {
namespace {

absl::Status RunCompletionServer(const ServerConfig& config,
                                 ::grpc::ServerBuilder* builder) {
  auto model_status = models::MakeModelHub(config.model_hub_config());
  if (!model_status.ok()) return model_status.status();
  ServerAsyncImpl server(std::move(model_status.value()));
  if (!server.StartServer(config.address_uri(), config.wait_for_clients(),
                          builder)) {
    return absl::InternalError("Server initialization failed");
  }
  return absl::OkStatus();;
}

}  // namespace

void InitConfigDefaults(ServerConfig* config) {
  config->set_wait_for_clients(true);
  if (config->address_uri().empty()) {
    config->set_address_uri(kDefaultServerAddress);
  }
}

absl::Status RunServer(const ServerConfig& config) {
  std::shared_ptr<::grpc::ServerCredentials> creds;
  switch (config.auth().credential_type()) {
    case AuthConfig::SSL:
      // TODO: setup SSL credentials.
      creds = ::grpc::InsecureServerCredentials();
      break;
    case AuthConfig::INSECURE:
      creds = ::grpc::InsecureServerCredentials();
      break;
    default:
      return absl::InvalidArgumentError("Unknown credential type");
  }
  ::grpc::ServerBuilder builder;
  builder.AddListeningPort(config.address_uri(), creds);
  return RunCompletionServer(config, &builder);
}

}  // namespace grpc
}  // namespace mozolm
