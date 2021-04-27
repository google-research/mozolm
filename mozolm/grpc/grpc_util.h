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

#ifndef MOZOLM_MOZOLM_GRPC_GRPC_UTIL_H_
#define MOZOLM_MOZOLM_GRPC_GRPC_UTIL_H_

#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "mozolm/grpc/grpc_util.pb.h"

ABSL_DECLARE_FLAG(double, mozolm_client_timeout);

namespace mozolm {
namespace grpc {

const char kDefaultServerPort[] = "localhost:50051";

// Sets default parameters if they have not already been set.
void ClientServerConfigDefaults(ClientServerConfig* config);

// Launches a language model server according to configuration.
absl::Status RunServer(const ClientServerConfig& grpc_config);

// Runs client service according to given configuration.
absl::Status RunClient(const ClientServerConfig& grpc_config);

}  // namespace grpc
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_GRPC_GRPC_UTIL_H_
