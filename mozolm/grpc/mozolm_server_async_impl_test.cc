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

#include <string>
#include <vector>

#include "include/grpcpp/grpcpp.h"
#include "include/grpcpp/server_context.h"
#include "gmock/gmock.h"
#include "mozolm/stubs/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "mozolm/grpc/mozolm_server_async_impl.h"
#include "mozolm/grpc/service.grpc.pb.h"
#include "mozolm/models/model_config.pb.h"
#include "mozolm/models/model_factory.h"
#include "mozolm/models/model_storage.pb.h"
#include "mozolm/utils/utf8_util.h"

namespace mozolm {
namespace grpc {
namespace {

constexpr float kFloatDelta = 0.00001;  // Delta for float comparisons.

using ::grpc::Status;
using ::grpc::ServerContext;

class MozoLMServerAsyncImplMock : public MozoLMServerAsyncImpl {
 public:
  MozoLMServerAsyncImplMock()
      : MozoLMServerAsyncImpl(models::MakeModelHub(ModelHubConfig()).value()) {}
};

// Check that a call to GetLMScores returns the expected status error code.
void CheckGetLMScoresError(int state, ::grpc::StatusCode error_code) {
  MozoLMServerAsyncImplMock server;
  ServerContext context;
  GetContextRequest request;
  LMScores response;
  request.set_state(state);
  const GetContextRequest* request_ptr(&request);
  Status status = server.HandleRequest(&context, request_ptr, &response);
  ASSERT_EQ(status.error_code(), error_code);
}

// Check that a call to GetLMScores succeeds and returns the expected
// probabilities and normalization.
void CheckGetLMScores(int state, const std::string& context_str = "") {
  MozoLMServerAsyncImplMock server;
  ServerContext context;
  GetContextRequest request;
  LMScores response;
  request.set_state(state);
  request.set_context(context_str);
  const GetContextRequest* request_ptr(&request);
  Status status = server.HandleRequest(&context, request_ptr, &response);
  ASSERT_TRUE(status.ok());
  ASSERT_EQ(response.probabilities_size(), 28);
  ASSERT_NEAR(response.normalization(), 28.0, kFloatDelta);
  double uniform_value = static_cast<double>(1.0) / static_cast<double>(28);
  for (int i = 0; i < 28; i++) {
    ASSERT_NEAR(response.probabilities(i), uniform_value, kFloatDelta);
  }
}

// Check that a call to UpdateLMScores returns the expected status code.
void CheckUpdateLMScoresError(int state, int utf8_sym, int count,
                              ::grpc::StatusCode error_code) {
  MozoLMServerAsyncImplMock server;
  ServerContext context;
  UpdateLMScoresRequest request;
  LMScores response;
  request.set_state(state);
  request.add_utf8_sym(utf8_sym);
  request.set_count(count);
  const UpdateLMScoresRequest* request_ptr(&request);
  Status status = server.HandleRequest(&context, request_ptr, &response);
  ASSERT_EQ(status.error_code(), error_code);
}

// Check that a call to UpdateLMScore updates correctly.
void CheckUpdateLMScoresContent(int state, int count) {
  MozoLMServerAsyncImplMock server;
  ServerContext context;
  UpdateLMScoresRequest request;
  LMScores response;
  request.set_state(state);
  // same symbol as context for ease of checking updates.
  const std::string symbol =
      utf8::EncodeUnicodeChar(server.ModelStateSym(state));
  request.add_utf8_sym(server.ModelStateSym(state));
  request.set_count(count);
  const UpdateLMScoresRequest* request_ptr(&request);
  Status status = server.HandleRequest(&context, request_ptr, &response);
  ASSERT_TRUE(status.ok());
  // Check if the response matches the request
  ASSERT_EQ(response.probabilities_size(), 28);
  ASSERT_EQ(response.symbols_size(), 28);
  ASSERT_NEAR(response.normalization(), 28.0 + count, kFloatDelta);
  double rest_value =
      static_cast<double>(1.0) / static_cast<double>(28 + count);
  for (int i = 0; i < 28; i++) {
    if (response.symbols(i) == symbol) {
      ASSERT_NEAR(
          response.probabilities(i),
          static_cast<double>(1 + count) / static_cast<double>(28 + count),
          kFloatDelta);
    } else {
      ASSERT_NEAR(response.probabilities(i), rest_value, kFloatDelta);
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
  CheckGetLMScores(0, "abcxyzff");
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
  CheckUpdateLMScoresContent(0, 1);
}

TEST(MozoLMServerAsyncTest, UpdateLMScore_SetsLMScoreWitCountTen) {
  CheckUpdateLMScoresContent(0, 10);
}

}  // namespace grpc
}  // namespace mozolm
