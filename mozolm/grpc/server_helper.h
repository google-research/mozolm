// Copyright 2022 MozoLM Authors.
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

#ifndef MOZOLM_MOZOLM_GRPC_SERVER_HELPER_H_
#define MOZOLM_MOZOLM_GRPC_SERVER_HELPER_H_

#include <memory>
#include <thread>

#include "absl/status/status.h"
#include "mozolm/grpc/server_async_impl.h"
#include "mozolm/grpc/server_config.pb.h"

namespace mozolm {
namespace grpc {

const char kDefaultServerAddress[] = "localhost:50051";

class ServerHelper {
 public:
  ServerHelper() = default;
  ~ServerHelper() { Shutdown(); }

  // Initializes the server given the configuration.
  absl::Status Init(const ServerConfig &config);

  // Runs the main request processing loop. If `wait_til_terminated` is enabled,
  // this call blocks until the server is terminated and the thread exists.
  // Otherwise starts the event processing loop and exists immediately.
  absl::Status Run(bool wait_till_terminated);

  // Shuts down the server. Mostly useful for the tests.
  void Shutdown();

  const ServerAsyncImpl &server() const { return *server_; }

 private:
  // The actual gRPC server.
  std::unique_ptr<ServerAsyncImpl> server_;

  // Thread for running the main processing loop.
  std::unique_ptr<std::thread> server_thread_;
};

// Sets default parameters for the server if they have not already been set.
void InitConfigDefaults(ServerConfig *config);

// Launches a language model server according to configuration.
absl::Status RunServer(const ServerConfig &config);

}  // namespace grpc
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_GRPC_SERVER_HELPER_H_
