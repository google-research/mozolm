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

#include <string>
#include <vector>

#include "include/grpcpp/grpcpp.h"
#include "include/grpcpp/server_context.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mozolm/grpc/mozolm_server_async_impl.h"
#include "mozolm/grpc/service.grpc.pb.h"

namespace mozolm {
namespace grpc {
namespace {

using ::grpc::Status;
using ::grpc::ServerContext;

// Check that a call to GetLMScores returns the expected status error code.
void CheckGetLMScoresError(int state, ::grpc::StatusCode error_code) {
  MozoLMServerAsyncImpl server;
  ServerContext context;
  GetContextRequest request;
  LMScores response;
  request.set_state(state);
  const GetContextRequest* request_ptr(&request);
  Status status = server.HandleRequest(&context, request_ptr, &response);
  ASSERT_EQ(status.error_code(), error_code);
}

// Check that a call to GetLMScores succeeds and returns the expected
// counts and normalization.
void CheckGetLMScores(int state) {
  MozoLMServerAsyncImpl server;
  ServerContext context;
  GetContextRequest request;
  LMScores response;
  request.set_state(state);
  const GetContextRequest* request_ptr(&request);
  Status status = server.HandleRequest(&context, request_ptr, &response);
  ASSERT_TRUE(status.ok());
  ASSERT_EQ(response.counts_size(), 28);
  ASSERT_EQ(response.normalization(), 28);
  for (int i = 0; i < 28; i++) {
    ASSERT_EQ(response.counts(i), 1);
  }
}

// Check that a call to UpdateLMScores returns the expected status code.
void CheckUpdateLMScoresError(int state, int utf8_sym, int count,
                              ::grpc::StatusCode error_code) {
  MozoLMServerAsyncImpl server;
  ServerContext context;
  UpdateLMScoresRequest request;
  LMScores response;
  request.set_state(state);
  request.set_utf8_sym(utf8_sym);
  request.set_count(count);
  const UpdateLMScoresRequest* request_ptr(&request);
  Status status = server.HandleRequest(&context, request_ptr, &response);
  ASSERT_EQ(status.error_code(), error_code);
}

// Check that a call to UpdateLMScore updates correctly.
void CheckUpdateLMScoresContent(int state, int count) {
  MozoLMServerAsyncImpl server;
  ServerContext context;
  UpdateLMScoresRequest request;
  LMScores response;
  request.set_state(state);
  // same symbol as context for ease of checking updates.
  request.set_utf8_sym(server.ModelStateSym(state));
  request.set_count(count);
  const UpdateLMScoresRequest* request_ptr(&request);
  Status status = server.HandleRequest(&context, request_ptr, &response);
  ASSERT_TRUE(status.ok());
  // Check if the response matches the request
  ASSERT_EQ(response.counts_size(), 28);
  ASSERT_EQ(response.normalization(), 28 + count);
  for (int i = 0; i < 28; i++) {
    if (i == state) {
      ASSERT_EQ(response.counts(i), 1 + count);
    } else {
      ASSERT_EQ(response.counts(i), 1);
    }
  }
}

}  // namespace

// The following tests are one-liners feeding in different sets of arguments
// to the fixture methods above.

TEST(MozoLMServerAsyncTest, GetLMScore_ReturnsAppErrorOnBadState) {
  CheckGetLMScoresError(999, ::grpc::StatusCode::INVALID_ARGUMENT);
}

TEST(MozoLMServerAsyncTest, GetLMScores_WorksOnStartState) {
  CheckGetLMScores(0);
}

TEST(MozoLMServerAsyncTest, GetLMScores_WorksOnOtherState) {
  CheckGetLMScores(9);
}

TEST(MozoLMServerAsyncTest, UpdateLMScore_ReturnsAppErrorOnBadSymbol) {
  CheckUpdateLMScoresError(2, 999, 1, ::grpc::StatusCode::INVALID_ARGUMENT);
}

TEST(MozoLMServerAsyncTest, UpdateLMScore_ReturnsAppErrorOnBadState) {
  CheckUpdateLMScoresError(-1, 1, 1, ::grpc::StatusCode::INVALID_ARGUMENT);
}

TEST(MozoLMServerAsyncTest, UpdateLMScore_ReturnsAppErrorOnBadCount) {
  CheckUpdateLMScoresError(2, 2, -1, ::grpc::StatusCode::INVALID_ARGUMENT);
}

TEST(MozoLMServerAsyncTest, UpdateLMScore_SetsLMScoreWitCountOne) {
  CheckUpdateLMScoresContent(1, 1);
}

TEST(MozoLMServerAsyncTest, UpdateLMScore_SetsLMScoreWitCountTen) {
  CheckUpdateLMScoresContent(1, 10);
}

}  // namespace grpc
}  // namespace mozolm
