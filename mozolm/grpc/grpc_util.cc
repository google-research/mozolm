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
#include "absl/strings/str_format.h"
#include "mozolm/grpc/client_helper.h"
#include "mozolm/grpc/server_async_impl.h"
#include "mozolm/models/model_factory.h"

ABSL_FLAG(double, mozolm_client_timeout, 10.0,
          "Timeout to wait for response in seconds.");

namespace mozolm {
namespace grpc {
namespace {

absl::Status RunCompletionServer(const ServerConfig& config,
                                 ::grpc::ServerBuilder* builder) {
  auto model_status = models::MakeModelHub(config.model_hub_config());
  if (!model_status.ok()) return model_status.status();
  ServerAsyncImpl server(std::move(model_status.value()));
  server.StartServer(config.port(), builder);
  return absl::OkStatus();;
}

}  // namespace

void InitConfigDefaults(ServerConfig* config) {
  if (config->port().empty()) {
    config->set_port(kDefaultServerPort);
  }
}

void InitConfigDefaults(ClientConfig* config) {
  InitConfigDefaults(config->mutable_server());
  if (config->timeout() <= 0.0) {
    config->set_timeout(absl::GetFlag(FLAGS_mozolm_client_timeout));
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
  builder.AddListeningPort(config.port(), creds);
  return RunCompletionServer(config, &builder);
}

absl::Status RunClient(const ClientConfig& config) {
  ClientHelper client(config);
  absl::Status status;
  std::string result;
  switch (config.request_type()) {
    case ClientConfig::RANDGEN:
      status = client.RandGen(config.context_string(), &result);
      break;
    case ClientConfig::K_BEST_ITEMS:
      status = client.OneKbestSample(config.k_best(), config.context_string(),
                                     &result);
      break;
    case ClientConfig::BITS_PER_CHAR_CALCULATION:
      status = client.CalcBitsPerCharacter(config.test_corpus(), &result);
      break;
    default:
      return absl::InvalidArgumentError("Unknown client request type");
  }
  if (status.ok()) {
    absl::PrintF("%s\n", result);
  }
  return status;
}

}  // namespace grpc
}  // namespace mozolm
