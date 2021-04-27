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

#include "mozolm/grpc/mozolm_client_async_impl.h"

#include <algorithm>

#include "mozolm/stubs/logging.h"
#include "include/grpcpp/client_context.h"
#include "include/grpcpp/completion_queue.h"
#include "include/grpcpp/support/async_stream.h"
#include "absl/memory/memory.h"
#include "mozolm/utils/utf8_util.h"

namespace mozolm {
namespace grpc {
namespace {

// Retrieves reverse probability sorted vector of prob/utf8-symbol pairs and
// normalization.
void RetrieveLMScores(
    const LMScores &response, double* normalization,
    std::vector<std::pair<double, std::string>>* prob_idx_pair_vector) {
  prob_idx_pair_vector->reserve(response.probabilities_size());
  for (int i = 0; i < response.probabilities_size(); i++) {
    prob_idx_pair_vector->push_back(
        std::make_pair(response.probabilities(i), response.symbols(i)));
    }
    std::sort(prob_idx_pair_vector->begin(), prob_idx_pair_vector->end());
    std::reverse(prob_idx_pair_vector->begin(), prob_idx_pair_vector->end());
  *normalization = response.normalization();
}

void WaitAndCheck(::grpc::CompletionQueue* cq) {
  void* got_tag;
  bool ok = false;
  GOOGLE_CHECK(cq->Next(&got_tag, &ok));  // Wait until next result arrives.
  GOOGLE_CHECK_EQ(got_tag, (void*)1);     // Verify result matches tag.
}

std::unique_ptr<::grpc::ClientContext> MakeClientContext(double timeout_ms) {
  std::unique_ptr<::grpc::ClientContext> context =
      absl::make_unique<::grpc::ClientContext>();
  context->set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME),
      gpr_time_from_millis(static_cast<int64>(1000.0 * timeout_ms),
                           GPR_TIMESPAN)));
  return context;
}

}  // namespace

MozoLMClientAsyncImpl::MozoLMClientAsyncImpl(
    std::unique_ptr<MozoLMServer::Stub> stub)
    : stub_(std::move(stub)) {}

absl::Status MozoLMClientAsyncImpl::GetLMScore(
    const std::string& context_str, int initial_state, double timeout,
    double* normalization,
    std::vector<std::pair<double, std::string>>* prob_idx_pair_vector) {
  // Sets up ClientContext, request and response.
  std::unique_ptr<::grpc::ClientContext> context = MakeClientContext(timeout);
  GetContextRequest request;
  LMScores response;
  request.set_state(initial_state);
  request.set_context(context_str);

  ::grpc::CompletionQueue cq;
  std::unique_ptr<::grpc::ClientAsyncResponseReader<LMScores>> rpc(
      stub_->AsyncGetLMScores(
          context.get(), request, &cq));  // Performs RPC call.
  ::grpc::Status status;
  rpc->Finish(&response, &status, reinterpret_cast<void*>(1));
  WaitAndCheck(&cq);
  if (!status.ok()) {
    return absl::InternalError(status.error_message());
  }
  // Retrieves information from response if RPC call was successful.
  RetrieveLMScores(response, normalization, prob_idx_pair_vector);
  return absl::OkStatus();
}

absl::Status MozoLMClientAsyncImpl::GetNextState(
    const std::string& context_str, int initial_state, double timeout,
    int64* next_state) {
  // Sets up ClientContext, request and response.
  std::unique_ptr<::grpc::ClientContext> context = MakeClientContext(timeout);
  GetContextRequest request;
  NextState response;
  request.set_state(initial_state);
  request.set_context(context_str);

  ::grpc::CompletionQueue cq;
  std::unique_ptr<::grpc::ClientAsyncResponseReader<NextState>> rpc(
      stub_->AsyncGetNextState(
          context.get(), request, &cq));  // Performs RPC call.
  ::grpc::Status status;
  rpc->Finish(&response, &status, reinterpret_cast<void*>(1));
  WaitAndCheck(&cq);

  if (!status.ok()) {
    return absl::InternalError(status.error_message());
  }
  // Sets next_state if RPC call was successful.
  *next_state = response.next_state();
  return absl::OkStatus();
}

absl::Status MozoLMClientAsyncImpl::UpdateCountGetDestStateScore(
    const std::string& context_str, int initial_state, double timeout,
    int count, int64* next_state, double* normalization,
    std::vector<std::pair<double, std::string>>* prob_idx_pair_vector) {
  const ::grpc::Status status = UpdateCountGetDestStateScore(
      utf8::StrSplitByCharToUnicode(context_str), initial_state, timeout, count,
      normalization, prob_idx_pair_vector);
  if (!status.ok()) {
    return absl::InternalError(status.error_message());
  }
  return GetNextState(context_str, initial_state, timeout, next_state);
}

::grpc::Status MozoLMClientAsyncImpl::UpdateCountGetDestStateScore(
    const std::vector<int>& context_str, int initial_state, double timeout,
    int count, double* normalization,
    std::vector<std::pair<double, std::string>>* prob_idx_pair_vector) {
  // Sets up ClientContext, request and response.
  std::unique_ptr<::grpc::ClientContext> context = MakeClientContext(timeout);
  UpdateLMScoresRequest request;
  LMScores response;
  request.set_state(initial_state);
  request.mutable_utf8_sym()->Reserve(context_str.size());
  for (auto utf8_sym : context_str) {
    request.add_utf8_sym(utf8_sym);
  }
  request.set_count(count);
  ::grpc::CompletionQueue cq;
  std::unique_ptr<::grpc::ClientAsyncResponseReader<LMScores>> rpc(
      stub_->AsyncUpdateLMScores(context.get(), request,
                                 &cq));  // Performs RPC call.
  ::grpc::Status status;
  rpc->Finish(&response, &status, reinterpret_cast<void*>(1));
  WaitAndCheck(&cq);

  if (status.ok()) {
    // Retrieves information from response if RPC call was successful.
    RetrieveLMScores(response, normalization, prob_idx_pair_vector);
  }
  return status;
}

}  // namespace grpc
}  // namespace mozolm
