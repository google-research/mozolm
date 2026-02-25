// Copyright 2026 MozoLM Authors.
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

#ifndef MOZOLM_MOZOLM_GRPC_CLIENT_ASYNC_IMPL_H_
#define MOZOLM_MOZOLM_GRPC_CLIENT_ASYNC_IMPL_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "mozolm/grpc/service.grpc.pb.h"

namespace mozolm {
namespace grpc {

// A completion-queue asynchronous client for the LM server.
class ClientAsyncImpl {
 public:
  // Constructs a client to use the given LM server.
  explicit ClientAsyncImpl(std::unique_ptr<MozoLMService::StubInterface> stub);

  // Seeks the language models scores given the initial state and context
  // string.
  absl::Status GetLMScore(
      const std::string& context_str, int initial_state, double timeout_sec,
      double* normalization,
      std::vector<std::pair<double, std::string>>* prob_idx_pair_vector);

  // Seeks the next model state given the initial state and context string.
  absl::Status GetNextState(const std::string& context_str, int initial_state,
                            double timeout_sec, int64_t* next_state);

  // Updates counts and retrieves probabilities from destination state.
  absl::Status UpdateCountGetDestStateScore(
      const std::string& context_str, int initial_state, double timeout_sec,
      int count, int64_t* next_state, double* normalization,
      std::vector<std::pair<double, std::string>>* prob_idx_pair_vector);

 private:
  ClientAsyncImpl() = delete;

  // Updates counts and retrieves probabilities from destination state.
  absl::Status UpdateCountGetDestStateScore(
      const std::vector<int>& context_str, int initial_state,
      double timeout_sec, int count, double* normalization,
      std::vector<std::pair<double, std::string>>* prob_idx_pair_vector);

  std::unique_ptr<MozoLMService::StubInterface> stub_;  // Owned elsewhere.
};

}  // namespace grpc
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_GRPC_CLIENT_ASYNC_IMPL_H_
