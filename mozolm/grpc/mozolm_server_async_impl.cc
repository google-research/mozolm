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

#include "mozolm/grpc/mozolm_server_async_impl.h"

#include <string>
#include <utility>

#include "mozolm/stubs/logging.h"
#include "include/grpcpp/server_builder.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/memory/memory.h"

ABSL_FLAG(int, mozolm_server_asynch_pool_size, 2,
          "number of threads in the UpdateLMScores handlers thread pool");

namespace mozolm {
namespace grpc {

using ::grpc::Status;
using ::grpc::ServerContext;

MozoLMServerAsyncImpl::MozoLMServerAsyncImpl(
    std::unique_ptr<models::LanguageModelHub> model_hub) {
  const int pool_size = absl::GetFlag(FLAGS_mozolm_server_asynch_pool_size);
  if (pool_size > 0) {
    asynch_pool_ = absl::make_unique<ThreadPool>(pool_size);
    asynch_pool_->StartWorkers();
  } else {
    asynch_pool_ = nullptr;
  }
  model_hub_ = std::move(model_hub);
}

Status MozoLMServerAsyncImpl::HandleRequest(ServerContext* context,
                                            const GetContextRequest* request,
                                            LMScores* response) {
  if (!model_hub_->ExtractLMScores(
          model_hub_->ContextState(request->context(), request->state()),
          response)) {
    // Only fails if given state is invalid.
    return Status(::grpc::StatusCode::INVALID_ARGUMENT, "invalid state");
  }
  return Status::OK;
}

Status MozoLMServerAsyncImpl::HandleRequest(ServerContext* context,
                                             const GetContextRequest* request,
                                             NextState* response) {
  int64 state = model_hub_->ContextState(request->context(), request->state());
  response->set_next_state(state);
  return Status::OK;
}

Status MozoLMServerAsyncImpl::HandleRequest(
    ServerContext* context, const UpdateLMScoresRequest* request,
    LMScores* response) {
  if (!ManageUpdateLMScores(request, response)) {
    return Status(::grpc::StatusCode::INVALID_ARGUMENT,
                  "State or utf8 symbol invalid");
  } else {
    return Status::OK;
  }
}

void MozoLMServerAsyncImpl::DriveCQ() {
  void* tag;  // Matches the async operation started against this cq_.
  bool ok;
  while (true) {
    // Waits for the completion of the next operation in cq_. Then, if not
    // shutting down, it casts the tag (a pointer to the std::function
    // implementing the next step in the execution of the RPC) to a
    // std::function and runs it inline.
    if (!cq_->Next(&tag, &ok)) {
      // The completion queue is shutting down.
      GOOGLE_LOG(INFO) << "Completion queue shutdown.";
      break;
    }
    // Casts the tag to a std::function and runs it inline.
    auto func_ptr = static_cast<std::function<void(bool)>*>(tag);
    (*func_ptr)(ok);
    delete func_ptr;
  }
}

bool MozoLMServerAsyncImpl::IncrementRpcPending() {
  absl::MutexLock lock(&server_shutdown_lock_);
  if (server_shutdown_) return false;
  ++rpcs_pending_;
  return true;
}

bool MozoLMServerAsyncImpl::DecrementRpcPending() {
  absl::MutexLock lock(&server_shutdown_lock_);
  rpcs_pending_--;
  if (rpcs_pending_ == 0) {
    // If count is zero, then server is shutdown (because new RPCs are requested
    // as part of this process if possible).
    GOOGLE_LOG(INFO) << "Notifying server of RPC completion.";
    rpcs_completed_.Notify();
  }
  return true;
}

void MozoLMServerAsyncImpl::RequestNextGetNextState() {
  if (!IncrementRpcPending()) {
    GOOGLE_LOG(INFO) << "Server shutdown, so not requesting GetNextState";
    return;
  }
  ServerContext* ctx = new ServerContext();
  GetContextRequest* request = new GetContextRequest();
  ::grpc::ServerAsyncResponseWriter<NextState>* responder =
        new ::grpc::ServerAsyncResponseWriter<NextState>(ctx);
  auto process_get_nextstate_callback = new std::function<void(bool)>(
      absl::bind_front(&MozoLMServerAsyncImpl::ProcessGetNextState, this, ctx,
                       request, responder));
  service_.RequestGetNextState(ctx, request, responder, cq_.get(), cq_.get(),
                               process_get_nextstate_callback);
}

void MozoLMServerAsyncImpl::ProcessGetNextState(
    ServerContext* ctx, GetContextRequest* request,
    ::grpc::ServerAsyncResponseWriter<NextState>* responder, bool ok) {
  if (!ok) {
    // Request for new RPC has failed, cleaning up and returning.
    GOOGLE_LOG(INFO) << "GetNextState not ok.";
    CleanupAfterGetNextState(ctx, request, responder, ok);
    return;
  }
  RequestNextGetNextState();  // Starts waiting for any new requests.
  NextState response;
  ::grpc::Status status = HandleRequest(ctx, request, &response);
  auto finish_get_nextstate_callback = new std::function<void(bool)>(
      absl::bind_front(&MozoLMServerAsyncImpl::CleanupAfterGetNextState, this,
                       ctx, request, responder));
  responder->Finish(response, status, finish_get_nextstate_callback);
}

void MozoLMServerAsyncImpl::CleanupAfterGetNextState(
    ServerContext* ctx, GetContextRequest* request,
    ::grpc::ServerAsyncResponseWriter<NextState>* responder, bool ignored_ok) {
  delete ctx;
  delete request;
  delete responder;
  DecrementRpcPending();
}

void MozoLMServerAsyncImpl::RequestNextGetLMScore() {
  if (!IncrementRpcPending()) {
    GOOGLE_LOG(INFO) << "Server shutdown, so not requesting GetLMScore";
    return;
  }
  ServerContext* ctx = new ServerContext();
  GetContextRequest* request = new GetContextRequest();
  ::grpc::ServerAsyncResponseWriter<LMScores>* responder =
        new ::grpc::ServerAsyncResponseWriter<LMScores>(ctx);
  auto process_get_lmscore_callback = new std::function<void(bool)>(
      absl::bind_front(&MozoLMServerAsyncImpl::ProcessGetLMScore, this, ctx,
                       request, responder));
  service_.RequestGetLMScores(ctx, request, responder, cq_.get(), cq_.get(),
                             process_get_lmscore_callback);
}

void MozoLMServerAsyncImpl::ProcessGetLMScore(
    ServerContext* ctx, GetContextRequest* request,
    ::grpc::ServerAsyncResponseWriter<LMScores>* responder, bool ok) {
  if (!ok) {
    // Request for new RPC has failed, cleaning up and returning.
    GOOGLE_LOG(INFO) << "GetLMScore not ok.";
    CleanupAfterGetLMScore(ctx, request, responder, ok);
    return;
  }
  RequestNextGetLMScore();  // Starts waiting for any new requests.
  LMScores response;
  ::grpc::Status status = HandleRequest(ctx, request, &response);
  auto finish_get_lmscore_callback = new std::function<void(bool)>(
      absl::bind_front(&MozoLMServerAsyncImpl::CleanupAfterGetLMScore, this,
                       ctx, request, responder));
  responder->Finish(response, status, finish_get_lmscore_callback);
}

void MozoLMServerAsyncImpl::CleanupAfterGetLMScore(
    ServerContext* ctx, GetContextRequest* request,
    ::grpc::ServerAsyncResponseWriter<LMScores>* responder, bool ignored_ok) {
  delete ctx;
  delete request;
  delete responder;
  DecrementRpcPending();
}

void MozoLMServerAsyncImpl::RequestNextUpdateLMScores() {
  if (!IncrementRpcPending()) {
    GOOGLE_LOG(INFO) << "Server shutdown, so not requesting GetLMScore";
    return;
  }
  ServerContext* ctx = new ServerContext();
  UpdateLMScoresRequest* request = new UpdateLMScoresRequest();
  ::grpc::ServerAsyncResponseWriter<LMScores>* responder =
        new ::grpc::ServerAsyncResponseWriter<LMScores>(ctx);
  auto process_update_lmscores_callback = new std::function<void(bool)>(
      absl::bind_front(&MozoLMServerAsyncImpl::ProcessUpdateLMScores, this,
                       ctx, request, responder));
  service_.RequestUpdateLMScores(ctx, request, responder, cq_.get(), cq_.get(),
                                 process_update_lmscores_callback);
}

void MozoLMServerAsyncImpl::ProcessUpdateLMScores(
    ServerContext* ctx, UpdateLMScoresRequest* request,
    ::grpc::ServerAsyncResponseWriter<LMScores>* responder, bool ok) {
  if (!ok) {
    // Request for new RPC has failed, cleaning up and returning.
    GOOGLE_LOG(INFO) << "UpdateLMScores not ok.";
    CleanupAfterUpdateLMScores(ctx, request, responder, ok);
    return;
  }
  RequestNextUpdateLMScores();  // Starts waiting for any new requests.
  LMScores response;
  ::grpc::Status status = HandleRequest(ctx, request, &response);
  auto finish_update_lmscores_callback = new std::function<void(bool)>(
      absl::bind_front(&MozoLMServerAsyncImpl::CleanupAfterUpdateLMScores,
                       this, ctx, request, responder));
  responder->Finish(response, status, finish_update_lmscores_callback);
}

void MozoLMServerAsyncImpl::CleanupAfterUpdateLMScores(
    ServerContext* ctx, UpdateLMScoresRequest* request,
    ::grpc::ServerAsyncResponseWriter<LMScores>* responder, bool ignored_ok) {
  delete ctx;
  delete request;
  delete responder;
  DecrementRpcPending();
}

::grpc::Server* MozoLMServerAsyncImpl::GetServer() {
  absl::MutexLock lock(&server_shutdown_lock_);
  server_shutdown_ = true;
  if (server_ == nullptr) {
    return nullptr;
  }
  return server_.get();
}

bool MozoLMServerAsyncImpl::StartWithCompletionQueue(
    ::grpc::ServerBuilder* builder) {
  absl::MutexLock lock(&server_shutdown_lock_);
  if (!server_shutdown_) {
    cq_ = builder->AddCompletionQueue();
    server_ = builder->BuildAndStart();
  }
  return !server_shutdown_;
}

bool MozoLMServerAsyncImpl::StartServer(const std::string& server_port,
                                         ::grpc::ServerBuilder* builder) {
  builder->RegisterService(&service_);
  if (StartWithCompletionQueue(builder)) {
    std::cout << "Server listening on " << server_port << std::endl;

    // Requests one RPC of each type to start the queue going.
    RequestNextGetNextState();
    RequestNextGetLMScore();

    // Proceed to the server's main loop.
    DriveCQ();
  }

  return true;
}

bool MozoLMServerAsyncImpl::ManageUpdateLMScores(
    const UpdateLMScoresRequest* request, LMScores* response) {
  const int utf8_sym_size = request->utf8_sym_size();
  std::vector<int> utf8_syms(utf8_sym_size);
  int curr_state = request->state();
  for (int i = 0; i < utf8_sym_size; ++i) {
    // Adds each symbol to vector and finds next state.
    utf8_syms[i] = request->utf8_sym(i);
    curr_state = model_hub_->NextState(curr_state, utf8_syms[i]);
  }
  return model_hub_->UpdateLMCounts(request->state(), utf8_syms,
                                    request->count()) &&
         model_hub_->ExtractLMScores(curr_state, response);
}

}  // namespace grpc
}  // namespace mozolm
