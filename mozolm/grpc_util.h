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

#ifndef MOZOLM_GRPC_UTIL_H__
#define MOZOLM_GRPC_UTIL_H__

#include <string>

#include "absl/flags/declare.h"
#include "mozolm/grpc_util.pb.h"

ABSL_DECLARE_FLAG(double, mozolm_client_timeout);

namespace mozolm {
namespace grpc {

const char kDefaultServerPort[] = "localhost:50051";

// Sets default parameters if they have not already been set.
void ClientServerConfigDefaults(ClientServerConfig* config);

// Launches a language model server according to configuration.  Optionally can
// also run a client to query the launched server, permitting single process
// launch and query.
bool RunServer(const ClientServerConfig& grpc_config, bool run_client = false,
               int k_best = 1, bool randgen = false,
               const std::string& context_string = "");

bool RunClient(const ClientServerConfig& grpc_config, int k_best, bool randgen,
               const std::string& context_string);

}  // namespace grpc
}  // namespace mozolm

#endif  // MOZOLM_GRPC_UTIL_H__
