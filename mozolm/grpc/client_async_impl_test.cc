// Copyright 2024 MozoLM Authors.
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

// Unit test for the client interface.

#include "mozolm/grpc/client_async_impl.h"

#include <memory>

#include "gmock/gmock.h"
#include "nisaba/port/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "include/grpcpp/alarm.h"
#include "include/grpcpp/grpcpp.h"  // IWYU pragma: keep
#include "mozolm/grpc/service.grpc.pb.h"
#include "mozolm/grpc/service_mock.grpc.pb.h"

using ::testing::_;
using ::testing::DoAll;
using ::protobuf_matchers::EqualsProto;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::Unused;

namespace mozolm {
namespace grpc {
namespace {

constexpr int kDefaultTimeoutSec = 1.0;

// Mock for the asynchronous response reader.
template <class R>
class LocalMockClientAsyncResponseReader
    : public ::grpc::ClientAsyncResponseReaderInterface<R> {
 public:
  LocalMockClientAsyncResponseReader() = default;

  /// ClientAsyncResponseReaderInterface.
  MOCK_METHOD(void, StartCall, ());
  MOCK_METHOD(void, ReadInitialMetadata, (void *));
  MOCK_METHOD(void, Finish, (R *, ::grpc::Status*, void *));

  // The tests below delete the reader that they create, rather than handing
  // ownership to the stub caller, so our Destroy method (which is used by the
  // stub caller) must do nothing. See
  // https://github.com/grpc/proposal/pull/238.
  virtual void Destroy() {}
};

class ClientAsyncImplTest : public ::testing::Test {
 protected:
  void SetUp() override {
    stub_ = new MockMozoLMServiceStub();
    client_ = std::make_unique<ClientAsyncImpl>(
        std::unique_ptr<MockMozoLMServiceStub>(stub_));
  }

  MockMozoLMServiceStub *stub_;  // Owned by the client.
  std::unique_ptr<ClientAsyncImpl> client_;
};

// TODO: Currently we only have the following simple example
// demonstrating injection of the mock stub interface that allows us
// to test the client channel without making actual RPC calls. Extend.
TEST_F(ClientAsyncImplTest, CheckGetNextStateUnaryAsync) {
  // Request message.
  GetContextRequest request;
  request.set_state(-1);
  request.set_context("");

  // Set up call expectations.
  int tag = 1;
  std::unique_ptr<::grpc::Alarm> alarm;
  auto reader =
      std::make_unique<LocalMockClientAsyncResponseReader<NextState>>();
  EXPECT_CALL(*stub_, AsyncGetNextStateRaw(_, EqualsProto(request), _))
    .WillOnce(
        DoAll(
            Invoke([&tag, &alarm](Unused, Unused, ::grpc::CompletionQueue *cq) {
              // This is a work-around to make caller's completion queue
              // `Next()` call succeed.
              alarm = std::make_unique<::grpc::Alarm>(
                  cq, gpr_now(GPR_CLOCK_MONOTONIC),
                  reinterpret_cast<void *>(&tag));
            }),
            Return(reader.get())));

  NextState response;
  EXPECT_CALL(*reader, Finish)
     .WillOnce(
         DoAll(
             Invoke([response, tag](NextState *result, ::grpc::Status *status,
                                    void *tag_ptr) {
               // This injects the actual response.
               *result = response;
               *status = ::grpc::Status::OK;
               *static_cast<int *>(tag_ptr) = tag;
             }),
             Return()));

  // Verify the response.
  int64_t next_state;
  EXPECT_OK(client_->GetNextState(request.context(), request.state(),
                                  kDefaultTimeoutSec, &next_state));
  EXPECT_EQ(0, next_state);
}

}  // namespace
}  // namespace grpc
}  // namespace mozolm
