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

#include "mozolm/grpc/server_async_impl.h"

#include <utility>
#include <vector>

#include "google/protobuf/stubs/logging.h"
#include "include/grpcpp/server_builder.h"
#include "absl/functional/bind_front.h"
#include "absl/memory/memory.h"
#include "absl/strings/match.h"

namespace mozolm {
namespace grpc {

using ::grpc::Status;
using ::grpc::ServerContext;

ServerAsyncImpl::ServerAsyncImpl(
    std::unique_ptr<models::LanguageModelHub> model_hub) {
  model_hub_ = std::move(model_hub);
}

Status ServerAsyncImpl::HandleRequest(ServerContext* context,
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

Status ServerAsyncImpl::HandleRequest(ServerContext* context,
                                      const GetContextRequest* request,
                                      NextState* response) {
  const int64 state = model_hub_->ContextState(request->context(),
                                               request->state());
  response->set_next_state(state);
  return Status::OK;
}

Status ServerAsyncImpl::HandleRequest(
    ServerContext* context, const UpdateLMScoresRequest* request,
    LMScores* response) {
  return ManageUpdateLMScores(request, response);
}

void ServerAsyncImpl::DriveCQ() {
  void* tag;  // Matches the async operation started against this cq_.
  bool ok;
  // Waits for the completion of the next operation in the queue. Then, if not
  // shutting down, it casts the tag (a pointer to the std::function
  // implementing the next step in the execution of the RPC) to a
  // std::function and runs it inline.
  while (cq_->Next(&tag, &ok)) {
    // Casts the tag to a std::function and runs it inline.
    auto func_ptr = static_cast<std::function<void(bool)>*>(tag);
    (*func_ptr)(ok);
    delete func_ptr;
  }
  // The completion queue is shutting down.
  GOOGLE_LOG(INFO) << "Completion queue shutdown.";
}

bool ServerAsyncImpl::IncrementRpcPending() {
  absl::MutexLock lock(&shutdown_lock_);
  if (server_shutdown_) return false;
  ++rpcs_pending_;
  return true;
}

bool ServerAsyncImpl::DecrementRpcPending() {
  absl::MutexLock lock(&shutdown_lock_);
  rpcs_pending_--;
  if (rpcs_pending_ == 0) {
    // If count is zero, then server is shutdown (because new RPCs are requested
    // as part of this process if possible).
    GOOGLE_LOG(INFO) << "Notifying server of RPC completion.";
    if (!rpcs_completed_.HasBeenNotified()) rpcs_completed_.Notify();
  }
  return true;
}

void ServerAsyncImpl::RequestNextGetNextState() {
  if (!IncrementRpcPending()) {
    GOOGLE_LOG(INFO) << "Server shutdown, so not requesting GetNextState";
    return;
  }
  ServerContext* ctx = new ServerContext();
  GetContextRequest* request = new GetContextRequest();
  ::grpc::ServerAsyncResponseWriter<NextState>* responder =
        new ::grpc::ServerAsyncResponseWriter<NextState>(ctx);
  auto process_get_nextstate_callback = new std::function<void(bool)>(
      absl::bind_front(&ServerAsyncImpl::ProcessGetNextState, this, ctx,
                       request, responder));
  service_.RequestGetNextState(ctx, request, responder, cq_.get(), cq_.get(),
                               process_get_nextstate_callback);
}

void ServerAsyncImpl::ProcessGetNextState(
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
      absl::bind_front(&ServerAsyncImpl::CleanupAfterGetNextState, this,
                       ctx, request, responder));
  responder->Finish(response, status, finish_get_nextstate_callback);
}

void ServerAsyncImpl::CleanupAfterGetNextState(
    ServerContext* ctx, GetContextRequest* request,
    ::grpc::ServerAsyncResponseWriter<NextState>* responder, bool ignored_ok) {
  delete ctx;
  delete request;
  delete responder;
  DecrementRpcPending();
}

void ServerAsyncImpl::RequestNextGetLMScore() {
  if (!IncrementRpcPending()) {
    GOOGLE_LOG(INFO) << "Server shutdown, so not requesting GetLMScore";
    return;
  }
  ServerContext* ctx = new ServerContext();
  GetContextRequest* request = new GetContextRequest();
  ::grpc::ServerAsyncResponseWriter<LMScores>* responder =
        new ::grpc::ServerAsyncResponseWriter<LMScores>(ctx);
  auto process_get_lmscore_callback = new std::function<void(bool)>(
      absl::bind_front(&ServerAsyncImpl::ProcessGetLMScore, this, ctx,
                       request, responder));
  service_.RequestGetLMScores(ctx, request, responder, cq_.get(), cq_.get(),
                             process_get_lmscore_callback);
}

void ServerAsyncImpl::ProcessGetLMScore(
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
      absl::bind_front(&ServerAsyncImpl::CleanupAfterGetLMScore, this,
                       ctx, request, responder));
  responder->Finish(response, status, finish_get_lmscore_callback);
}

void ServerAsyncImpl::CleanupAfterGetLMScore(
    ServerContext* ctx, GetContextRequest* request,
    ::grpc::ServerAsyncResponseWriter<LMScores>* responder, bool ignored_ok) {
  delete ctx;
  delete request;
  delete responder;
  DecrementRpcPending();
}

void ServerAsyncImpl::RequestNextUpdateLMScores() {
  if (!IncrementRpcPending()) {
    GOOGLE_LOG(INFO) << "Server shutdown, so not requesting GetLMScore";
    return;
  }
  ServerContext* ctx = new ServerContext();
  UpdateLMScoresRequest* request = new UpdateLMScoresRequest();
  ::grpc::ServerAsyncResponseWriter<LMScores>* responder =
        new ::grpc::ServerAsyncResponseWriter<LMScores>(ctx);
  auto process_update_lmscores_callback = new std::function<void(bool)>(
      absl::bind_front(&ServerAsyncImpl::ProcessUpdateLMScores, this,
                       ctx, request, responder));
  service_.RequestUpdateLMScores(ctx, request, responder, cq_.get(), cq_.get(),
                                 process_update_lmscores_callback);
}

void ServerAsyncImpl::ProcessUpdateLMScores(
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
      absl::bind_front(&ServerAsyncImpl::CleanupAfterUpdateLMScores,
                       this, ctx, request, responder));
  responder->Finish(response, status, finish_update_lmscores_callback);
}

void ServerAsyncImpl::CleanupAfterUpdateLMScores(
    ServerContext* ctx, UpdateLMScoresRequest* request,
    ::grpc::ServerAsyncResponseWriter<LMScores>* responder, bool ignored_ok) {
  delete ctx;
  delete request;
  delete responder;
  DecrementRpcPending();
}

absl::Status ServerAsyncImpl::BuildAndStart(
    const std::string& address_uri,
    std::shared_ptr<::grpc::ServerCredentials> creds,
    int async_pool_size) {
  absl::MutexLock lock(&shutdown_lock_);
  if (server_shutdown_) {
    return absl::InternalError("Cannot initialize in the middle of shutdown");
  }

  // Initialize asynchronous request handlers.
  // TODO: Complete.
  if (async_pool_size > 0) {
    async_pool_ = absl::make_unique<nisaba::ThreadPool>(async_pool_size);
    async_pool_->StartWorkers();
  } else {
    async_pool_ = nullptr;
  }

  // Initialize the server.
  ::grpc::ServerBuilder builder;
  selected_port_ = -1;
  builder.AddListeningPort(address_uri, creds, &selected_port_);
  builder.RegisterService(&service_);

  // Build the completion queue and start.
  cq_ = builder.AddCompletionQueue();
  server_ = builder.BuildAndStart();
  if (server_ == nullptr) {
    return absl::InternalError(
        "Failed to build the server. Check the log for errors");
  }
  rpcs_completed_.Notify();  // No RPCs yet.
  GOOGLE_LOG(INFO) << "Listening on \"" << address_uri << "\"";
  if (absl::EndsWith(address_uri, ":0")) {
    GOOGLE_LOG(INFO) << "Selected port: " << selected_port_;
  }
  return absl::OkStatus();
}

absl::Status ServerAsyncImpl::ProcessRequests() {
  // Requests one RPC of each type to start the queue going.
  RequestNextGetNextState();
  RequestNextGetLMScore();
  RequestNextUpdateLMScores();

  // Proceed to the server's main loop.
  DriveCQ();
  return absl::OkStatus();
}

void ServerAsyncImpl::Shutdown() {
  GOOGLE_LOG(INFO) << "Shutting down ...";
  ::grpc::Server* server;
  {
    absl::MutexLock lock(&shutdown_lock_);
    server_shutdown_ = true;
    // Get the server_ variable that is protected by a lock and capture it
    // in a local since we can't hold the lock while performing a Shutdown
    // since the RPC processing triggered by the shutdown will also want
    // this same lock.
    if (server_) {
      server = server_.get();
    } else {
      GOOGLE_LOG(INFO) << "Shut down requested before initialization";
      return;
    }
  }

  // Server shutdown causes the server to stop accepting new calls,
  // returns !ok for any requested calls that are not yet started, and
  // means that the application can no longer request new calls.
  server->Shutdown();
  // Now wait for all RPCs to complete their processing before we try to
  // shutdown the CQ. We do this because once the CQ is shutdown, we're
  // not supposed to enqueue new operations that require a CQ response,
  // such as the Finish operations that complete the RPC lifecycle.
  rpcs_completed_.WaitForNotification();
  // Shutdown the completion queue after the server is shutdown and all
  // outstanding RPCs are complete.
  cq_->Shutdown();

  // Drain the completion queue. The remaining requests will be fetched from
  // the inactive completion queue and their arguments freed.
  DriveCQ();
}

Status ServerAsyncImpl::ManageUpdateLMScores(
    const UpdateLMScoresRequest* request, LMScores* response) {
  const int utf8_sym_size = request->utf8_sym_size();
  std::vector<int> utf8_syms(utf8_sym_size);
  int curr_state = request->state();
  for (int i = 0; i < utf8_sym_size; ++i) {
    // Adds each symbol to vector and finds next state.
    utf8_syms[i] = request->utf8_sym(i);
    curr_state = model_hub_->NextState(curr_state, utf8_syms[i]);
  }
  if (!model_hub_->UpdateLMCounts(request->state(), utf8_syms,
                                  request->count())) {
    return Status(::grpc::StatusCode::INVALID_ARGUMENT,
                  "Failed to update language model counts.");
  }
  if (model_hub_->ExtractLMScores(curr_state, response)) {
    return Status::OK;
  } else {
    return Status(::grpc::StatusCode::INVALID_ARGUMENT,
                  "Failed to extract scores.");
  }
}

}  // namespace grpc
}  // namespace mozolm
