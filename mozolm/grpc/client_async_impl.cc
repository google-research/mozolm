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

#include "mozolm/grpc/client_async_impl.h"

#include "include/grpcpp/client_context.h"
#include "include/grpcpp/completion_queue.h"
#include "include/grpcpp/grpcpp.h"  // IWYU pragma: keep
#include "include/grpcpp/support/async_stream.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_format.h"
#include "mozolm/models/language_model.h"
#include "mozolm/utils/utf8_util.h"
#include "mozolm/stubs/status_macros.h"

namespace mozolm {
namespace grpc {
namespace {

// Extracts the next tag from a gRPC completion queue and verify that it matches
// the given expected tag. Returns OK upon a successful fetch and match.
absl::Status WaitAndCheck(::grpc::CompletionQueue *cq, int expected_tag) {
  // Wait until next result arrives.
  void *got_tag;
  bool ok = false;
  if (!cq->Next(&got_tag, &ok)) {
    return absl::InternalError("Completion queue next error");
  }
  if (!ok) return absl::InternalError("RPC call failed");

  // Match the tag with the expected tag.
  if (expected_tag != *static_cast<int *>(got_tag)) {
    return absl::InternalError(absl::StrFormat(
        "Completion queue tag mismatch. Expected %d, got %d",
        expected_tag, *static_cast<int *>(got_tag)));
  }
  return absl::OkStatus();
}

std::unique_ptr<::grpc::ClientContext> MakeClientContext(double timeout_sec) {
  std::unique_ptr<::grpc::ClientContext> context =
      absl::make_unique<::grpc::ClientContext>();
  context->set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME),
      gpr_time_from_millis(static_cast<int64>(1000.0 * timeout_sec),
                           GPR_TIMESPAN)));
  return context;
}

}  // namespace

ClientAsyncImpl::ClientAsyncImpl(
    std::unique_ptr<MozoLMService::StubInterface> stub) : stub_(
        std::move(stub)) {}

absl::Status ClientAsyncImpl::GetLMScore(
    const std::string& context_str, int initial_state, double timeout_sec,
    double* normalization,
    std::vector<std::pair<double, std::string>>* prob_idx_pair_vector) {
  // Sets up client context and the request.
  std::unique_ptr<::grpc::ClientContext> context = MakeClientContext(
      timeout_sec);
  GetContextRequest request;
  request.set_state(initial_state);
  request.set_context(context_str);

  // Fetches the response.
  ::grpc::CompletionQueue cq;
  std::unique_ptr<::grpc::ClientAsyncResponseReaderInterface<LMScores>> rpc(
      stub_->AsyncGetLMScores(
          context.get(), request, &cq));  // Performs RPC call.
  ::grpc::Status status;
  LMScores response;
  int finish_tag = 1;
  rpc->Finish(&response, &status, &finish_tag);
  RETURN_IF_ERROR(WaitAndCheck(&cq, finish_tag));
  if (!status.ok()) {
    return absl::InternalError(status.error_message());
  }

  // Retrieves information from response if RPC call was successful.
  ASSIGN_OR_RETURN(*prob_idx_pair_vector, models::GetTopHypotheses(response));
  *normalization = response.normalization();
  return absl::OkStatus();
}

absl::Status ClientAsyncImpl::GetNextState(
    const std::string& context_str, int initial_state, double timeout_sec,
    int64* next_state) {
  // Sets up client context and the request.
  std::unique_ptr<::grpc::ClientContext> context = MakeClientContext(
      timeout_sec);
  GetContextRequest request;
  request.set_state(initial_state);
  request.set_context(context_str);

  ::grpc::CompletionQueue cq;
  std::unique_ptr<::grpc::ClientAsyncResponseReaderInterface<NextState>> rpc(
      stub_->AsyncGetNextState(
          context.get(), request, &cq));  // Performs RPC call.
  if (!rpc) {  // This will fail if the test mocks are not set up correctly.
    return absl::InternalError("Got invalid response reader");
  }
  ::grpc::Status status;
  NextState response;
  int finish_tag = 1;
  rpc->Finish(&response, &status, &finish_tag);
  RETURN_IF_ERROR(WaitAndCheck(&cq, finish_tag));
  if (!status.ok()) {
    return absl::InternalError(status.error_message());
  }

  // Sets next_state if RPC call was successful.
  *next_state = response.next_state();
  return absl::OkStatus();
}

absl::Status ClientAsyncImpl::UpdateCountGetDestStateScore(
    const std::string& context_str, int initial_state, double timeout_sec,
    int count, int64* next_state, double* normalization,
    std::vector<std::pair<double, std::string>>* prob_idx_pair_vector) {
  RETURN_IF_ERROR(UpdateCountGetDestStateScore(
      utf8::StrSplitByCharToUnicode(context_str), initial_state, timeout_sec,
      count, normalization, prob_idx_pair_vector));
  return GetNextState(context_str, initial_state, timeout_sec, next_state);
}

absl::Status ClientAsyncImpl::UpdateCountGetDestStateScore(
    const std::vector<int>& context_str, int initial_state, double timeout_sec,
    int count, double* normalization,
    std::vector<std::pair<double, std::string>>* prob_idx_pair_vector) {
  // Sets up client context and the request.
  std::unique_ptr<::grpc::ClientContext> context = MakeClientContext(
      timeout_sec);
  UpdateLMScoresRequest request;
  request.set_state(initial_state);
  request.mutable_utf8_sym()->Reserve(context_str.size());
  for (auto utf8_sym : context_str) {
    request.add_utf8_sym(utf8_sym);
  }
  request.set_count(count);

  // Fetches the response.
  ::grpc::CompletionQueue cq;
  std::unique_ptr<::grpc::ClientAsyncResponseReaderInterface<LMScores>> rpc(
      stub_->AsyncUpdateLMScores(context.get(), request,
                                 &cq));  // Performs RPC call.
  ::grpc::Status status;
  LMScores response;
  int finish_tag = 1;
  rpc->Finish(&response, &status, &finish_tag);
  RETURN_IF_ERROR(WaitAndCheck(&cq, finish_tag));
  if (!status.ok()) return absl::InternalError(status.error_message());

  // Retrieves information from response if RPC call was successful.
  const auto probs_status = models::GetTopHypotheses(response);
  if (probs_status.ok()) {
    *prob_idx_pair_vector = std::move(probs_status.value());
    *normalization = response.normalization();
  }
  return probs_status.status();
}

}  // namespace grpc
}  // namespace mozolm
