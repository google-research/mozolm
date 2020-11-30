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

#ifndef MOZOLM_MOZOLM_GRPC_MOZOLM_SERVER_ASYNC_IMPL_H_
#define MOZOLM_MOZOLM_GRPC_MOZOLM_SERVER_ASYNC_IMPL_H_

#include <memory>
#include <string>

#include "include/grpcpp/grpcpp.h"
#include "include/grpcpp/server.h"
#include "include/grpcpp/server_context.h"
#include "include/grpcpp/support/async_stream.h"
#include "absl/flags/declare.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "mozolm/grpc/service.grpc.pb.h"
#include "mozolm/grpc/service.pb.h"
#include "mozolm/models/language_model.h"
#include "mozolm/stubs/thread_pool.h"

ABSL_DECLARE_FLAG(int, mozolm_server_asynch_pool_size);

namespace mozolm {
namespace grpc {

// A simple lm_score server, that can provide lm_scores given a context string
// or model state. Asynchronous, based on completion-queue.
class MozoLMServerAsyncImpl : public MozoLMServer::AsyncService {
  friend class MozoLMServerAsyncTest;

 public:
  // Creates and initializes the server, a thread pool is created to handle
  // requests if pool_size is > 0. An initialized instance of a language model
  // is required.
  MozoLMServerAsyncImpl(std::unique_ptr<models::LanguageModel> model);

  // TODO: look into server shutdown methods.
  ~MozoLMServerAsyncImpl() {
    if (server_ != nullptr) {
      server_->Shutdown();
      // Always shutdown the completion queue after the server.
      rpcs_completed_.WaitForNotification();  // Waits for completion.
      cq_->Shutdown();
    }
  }
  MozoLMServerAsyncImpl() = delete;

  // Returns the lm_scores given the context.
  ::grpc::Status HandleRequest(::grpc::ServerContext* context,
                               const GetContextRequest* request,
                               LMScores* response);

  // Returns the next state given the context.
  ::grpc::Status HandleRequest(::grpc::ServerContext* context,
                               const GetContextRequest* request,
                               NextState* response);

  // Updates the counts/norm by count and advances state, returning counts at
  // new state.
  ::grpc::Status HandleRequest(::grpc::ServerContext* context,
                               const UpdateLMScoresRequest* request,
                               LMScores* response);

  // Returns the model symbol index associated with a state.
  int ModelStateSym(int state) {
    return model_->StateSym(state);
  }

  bool StartServer(const std::string& server_port,
                   ::grpc::ServerBuilder* builder);

 private:
  void DriveCQ();              // Manages a step in the operation of cq_.
  bool IncrementRpcPending();  // Locks, increments & releases counter.
  bool DecrementRpcPending();  // Locks, decrements & releases counter.
  ::grpc::Server* GetServer();  // Returns server_ pointer for shutdown.

  // Starts completion queue and builds server.
  bool StartWithCompletionQueue(::grpc::ServerBuilder* builder);

  // Manages the UpdateLMScores steps.
  bool ManageUpdateLMScores(const UpdateLMScoresRequest* request,
                            LMScores* response);

  // Steps for handling a GetNextState request: 1) initializes request and
  // starts waiting for new requests; 2) processes and finishes received
  // requests; and 3) cleans up allocated data.
  void RequestNextGetNextState() ABSL_LOCKS_EXCLUDED(server_shutdown_lock_);
  void ProcessGetNextState(
      ::grpc::ServerContext* ctx, GetContextRequest* request,
      ::grpc::ServerAsyncResponseWriter<NextState>* responder, bool ok)
      ABSL_LOCKS_EXCLUDED(server_shutdown_lock_);
  void CleanupAfterGetNextState(
      ::grpc::ServerContext* ctx, GetContextRequest* request,
      ::grpc::ServerAsyncResponseWriter<NextState>* responder, bool ignored_ok)
      ABSL_LOCKS_EXCLUDED(server_shutdown_lock_);

  // Steps for handling a GetLMScore request: 1) initializes request and
  // starts waiting for new requests; 2) processes and finishes received
  // requests; and 3) cleans up allocated data.
  void RequestNextGetLMScore() ABSL_LOCKS_EXCLUDED(server_shutdown_lock_);
  void ProcessGetLMScore(
      ::grpc::ServerContext* ctx, GetContextRequest* request,
      ::grpc::ServerAsyncResponseWriter<LMScores>* responder, bool ok)
      ABSL_LOCKS_EXCLUDED(server_shutdown_lock_);
  void CleanupAfterGetLMScore(
      ::grpc::ServerContext* ctx, GetContextRequest* request,
      ::grpc::ServerAsyncResponseWriter<LMScores>* responder, bool ignored_ok)
      ABSL_LOCKS_EXCLUDED(server_shutdown_lock_);

  // Steps for handling an UpdateLMScores request: 1) initializes request and
  // starts waiting for new requests; 2) processes and finishes received
  // requests; and 3) cleans up allocated data.
  void RequestNextUpdateLMScores() ABSL_LOCKS_EXCLUDED(server_shutdown_lock_);
  void ProcessUpdateLMScores(
      ::grpc::ServerContext* ctx, UpdateLMScoresRequest* request,
      ::grpc::ServerAsyncResponseWriter<LMScores>* responder, bool ok)
      ABSL_LOCKS_EXCLUDED(server_shutdown_lock_);
  void CleanupAfterUpdateLMScores(
      ::grpc::ServerContext* ctx, UpdateLMScoresRequest* request,
      ::grpc::ServerAsyncResponseWriter<LMScores>* responder, bool ignored_ok)
      ABSL_LOCKS_EXCLUDED(server_shutdown_lock_);

  // Model instance owned by the server.
  std::unique_ptr<models::LanguageModel> model_;

  std::unique_ptr<::grpc::ServerCompletionQueue> cq_;
  MozoLMServer::AsyncService service_;
  absl::Mutex server_shutdown_lock_;
  std::unique_ptr<::grpc::Server> server_
      ABSL_GUARDED_BY(server_shutdown_lock_);

  // Indicates whether the server has been shutdown.
  bool server_shutdown_ ABSL_GUARDED_BY(server_shutdown_lock_) = false;

  // Count of the number of pending rpcs, incremented and decremented as
  // requests come in and are finished.
  int rpcs_pending_ ABSL_GUARDED_BY(server_shutdown_lock_) = 0;

  // Notified when pending rpc count is zero.
  absl::Notification rpcs_completed_;

  std::unique_ptr<ThreadPool> asynch_pool_;  // Pool for handling updates.
};

}  // namespace grpc
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_GRPC_MOZOLM_SERVER_ASYNC_IMPL_H_
