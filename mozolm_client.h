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

#ifndef MOZOLM_MOZOLM_CLIENT_H__
#define MOZOLM_MOZOLM_CLIENT_H__

#include <memory>
#include <string>
#include <vector>

#include "mozolm/stubs/integral_types.h"
#include "include/grpcpp/create_channel.h"
#include "third_party/mozolm/grpc_util.proto.h"
#include "third_party/mozolm/mozolm_client_async_impl.h"

namespace mozolm {
namespace grpc {

const int kMaxRandGenLen = 128;

class MozoLMClient {
 public:
  explicit MozoLMClient(const ClientServerConfig& grpc_config);

  // Generates a k-best list from the model given the context.
  bool OneKbestSample(int k_best, const std::string& context_string,
                      std::string* result);

  // Generates a random string prefixed by the context string.
  bool RandGen(const std::string& context_string, std::string* result);

 private:
  // Requests LMScores from model, populates vector of count/index pairs and
  // updates normalization count, returning true if successful.
  bool GetLMScores(const std::string& context_string, int initial_state,
                   int64* normalization,
                   std::vector<std::pair<int64, int32>>* count_idx_pair_vector);

  // Requests next state from model and returns result.
  int64 GetNextState(const std::string& context_string, int initial_state);

  double timeout_;  // Timeout when waiting for server.
  std::shared_ptr<::grpc::Channel> channel_;
  std::unique_ptr<MozoLMClientAsyncImpl> completion_client_;
};

}  // namespace grpc
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_CLIENT_H__
