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

#include "mozolm/grpc/client_helper.h"

#include "gmock/gmock.h"
#include "mozolm/stubs/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "mozolm/grpc/server_helper.h"

namespace mozolm {
namespace grpc {
namespace {

TEST(ClientHelperTest, CheckConfig) {
  ClientConfig config;
  InitConfigDefaults(&config);
  const ServerConfig &server_config = config.server();
  EXPECT_EQ(kDefaultServerAddress, server_config.address_uri());
  EXPECT_TRUE(server_config.wait_for_clients());
  EXPECT_LT(0, config.timeout_sec());
}

}  // namespace
}  // namespace grpc
}  // namespace mozolm
