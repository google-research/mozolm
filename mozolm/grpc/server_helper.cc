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

#include "mozolm/stubs/logging.h"
#include "include/grpcpp/security/server_credentials.h"
#include "absl/memory/memory.h"
#include "mozolm/models/model_factory.h"
#include "mozolm/stubs/status_macros.h"

namespace mozolm {
namespace grpc {
namespace {

// Worker thread for processing server requests in a completion queue.
void ProcessRequests(ServerAsyncImpl *server) {
  GOOGLE_LOG(INFO) << "Waiting for requests ...";
  const auto status = server->ProcessRequests();
  if (status.ok()) {
    GOOGLE_LOG(INFO) << "Server processing queue shut down OK.";
  } else {
    GOOGLE_LOG(ERROR) << "Server process queue shut down with error: "
               << status.ToString();
  }
}

}  // namespace

absl::Status ServerHelper::Init(const ServerConfig& config) {
  if (server_) return absl::InternalError("Server already active");

  // Initialize the model hub.
  auto model_status = models::MakeModelHub(config.model_hub_config());
  if (!model_status.ok()) return model_status.status();

  // Configure authentication.
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

  // Initialize the server and start the server.
  server_ = absl::make_unique<ServerAsyncImpl>(std::move(model_status.value()));
  return server_->BuildAndStart(config.address_uri(), creds);
}

absl::Status ServerHelper::Run(bool wait_till_terminated) {
  if (!server_) return absl::InternalError("Server not initialized");
  server_thread_ = absl::make_unique<std::thread>(&ProcessRequests,
                                                  server_.get());
  if (wait_till_terminated) server_thread_->join();
  return absl::OkStatus();
}

void ServerHelper::Shutdown() {
  if (server_) {
    server_->Shutdown();
    if (server_thread_) {
      if (server_thread_->joinable()) server_thread_->join();
      server_thread_.reset();
    }
    server_.reset();
  }
}

void InitConfigDefaults(ServerConfig* config) {
  config->set_wait_for_clients(true);
  if (config->address_uri().empty()) {
    config->set_address_uri(kDefaultServerAddress);
  }
}

absl::Status RunServer(const ServerConfig& config) {
  ServerHelper server;
  RETURN_IF_ERROR(server.Init(config));
  if (config.wait_for_clients()) {
    RETURN_IF_ERROR(server.Run(/* wait_till_terminated= */true));
  }
  return absl::OkStatus();
}

}  // namespace grpc
}  // namespace mozolm
