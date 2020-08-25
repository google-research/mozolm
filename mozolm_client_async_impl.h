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

#ifndef MOZOLM_MOZOLM_CLIENT_ASYNC_IMPL_H_
#define MOZOLM_MOZOLM_CLIENT_ASYNC_IMPL_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mozolm/stubs/integral_types.h"
#include "include/grpcpp/grpcpp.h"  // IWYU pragma: keep
#include "third_party/mozolm/lm_scores.grpc.pb.h"

namespace mozolm {
namespace grpc {

// A completion-queue asynchronous client for the lm_score server.
class MozoLMClientAsyncImpl {
 public:
  // Constructs a client to use the given lm_score server.
  explicit MozoLMClientAsyncImpl(std::unique_ptr<MozoLMServer::Stub> stub);

  // Seeks the language models scores given the initial state and context
  // string. Any errors are logged.
  bool GetLMScore(const std::string& context_str, int initial_state,
                  double timeout, int64* normalization,
                  std::vector<std::pair<int64, int32>>* count_idx_pair_vector);

  // Seeks the next model state given the initial state and context string. Any
  // errors are logged.
  bool GetNextState(const std::string& context_str, int initial_state,
                    double timeout, int64* next_state);

 private:
  std::unique_ptr<MozoLMServer::Stub> stub_;
};

}  // namespace grpc
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_CLIENT_ASYNC_IMPL_H_
