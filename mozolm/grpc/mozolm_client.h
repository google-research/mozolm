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

#ifndef MOZOLM_MOZOLM_GRPC_MOZOLM_CLIENT_H_
#define MOZOLM_MOZOLM_GRPC_MOZOLM_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include "mozolm/stubs/integral_types.h"
#include "include/grpcpp/create_channel.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mozolm/grpc/grpc_util.pb.h"
#include "mozolm/grpc/mozolm_client_async_impl.h"

namespace mozolm {
namespace grpc {

constexpr int kMaxRandGenLen = 128;

class MozoLMClient {
 public:
  explicit MozoLMClient(const ClientServerConfig& grpc_config);

  // Generates a k-best list from the model given the context.
  absl::Status OneKbestSample(int k_best, const std::string& context_string,
                              std::string* result);

  // Generates a random string prefixed by the context string.
  absl::Status RandGen(const std::string& context_string, std::string* result);

  // Calculates bits per character in test file.  To cover all unicode
  // codepoints, even those assigned zero probability by the model, we
  // interpolate with a uniform model over all codepoints, using a very small
  // interpolation factor for this mixing.
  absl::Status CalcBitsPerCharacter(const std::string& test_file,
                                    std::string* result);

 private:
  // Requests LMScores from model, populates vector of prob/index pairs and
  // updates normalization count, returning true if successful.
  absl::Status GetLMScores(
      const std::string& context_string, int initial_state,
      double* normalization,
      std::vector<std::pair<double, std::string>>* prob_idx_pair_vector);

  // Requests next state from model and returns result.
  absl::StatusOr<int64> GetNextState(const std::string& context_string,
                                     int initial_state);

  // Updates counts in model and returns destination state and prob/index pairs.
  absl::Status UpdateCountGetDestStateScore(
      const std::string& context_string, int initial_state, int32 count,
      int64* next_state, double* normalization,
      std::vector<std::pair<double, std::string>>* prob_idx_pair_vector);

  double timeout_;  // Timeout when waiting for server.
  std::shared_ptr<::grpc::Channel> channel_;
  std::unique_ptr<MozoLMClientAsyncImpl> completion_client_;
};

}  // namespace grpc
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_GRPC_MOZOLM_CLIENT_H_
