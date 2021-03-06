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

#include "mozolm/grpc/mozolm_client.h"

#include <string>
#include <vector>

#include "mozolm/stubs/integral_types.h"
#include "mozolm/stubs/logging.h"
#include "include/grpcpp/create_channel.h"
#include "include/grpcpp/grpcpp.h"
#include "include/grpcpp/security/credentials.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/notification.h"
#include "mozolm/grpc/grpc_util.pb.h"
#include "mozolm/grpc/mozolm_client_async_impl.h"
#include "mozolm/utils/utf8_util.h"

namespace mozolm {
namespace grpc {
namespace {

// Returns random probability threshold between 0 and 1.
double GetUniformThreshold() {
  absl::BitGen gen;
  return absl::Uniform(gen, 0, 100000) / static_cast<double>(100000);
}

// Uses random number to choose position according to returned distribution.
int GetRandomPosition(
    const std::vector<std::pair<double, int32>>& prob_idx_pair_vector) {
  const double thresh = GetUniformThreshold();
  double total_prob = 0.0;
  int pos = 0;
  while (total_prob < thresh &&
         pos < static_cast<int64>(prob_idx_pair_vector.size())) {
    total_prob +=
        static_cast<double>(prob_idx_pair_vector[pos++].first);
  }
  if (pos > 0) --pos;
  return pos;
}
}  // namespace

bool MozoLMClient::GetLMScores(
    const std::string& context_string, int initial_state, double* normalization,
    std::vector<std::pair<double, int32>>* prob_idx_pair_vector) {
  if (completion_client_ == nullptr) {
    GOOGLE_LOG(ERROR) << "completion_client_ not initialized.";
    return false;
  }
  if (!completion_client_->GetLMScore(context_string, initial_state,
                                              timeout_, normalization,
                                      prob_idx_pair_vector)) {
    GOOGLE_LOG(ERROR) << "Failed to retrieve LM score";
    return false;
  }
  return *normalization > 0;
}

int64 MozoLMClient::GetNextState(const std::string& context_string,
                                  int initial_state) {
  int64 next_state;
  GOOGLE_CHECK_NE(completion_client_, nullptr);
  GOOGLE_CHECK(completion_client_->GetNextState(context_string, initial_state,
                                                timeout_, &next_state));
  return next_state;
}

bool MozoLMClient::RandGen(const std::string& context_string,
                           std::string* result) {
  bool success = true;

  // The context string is the prefix to the randomly generated string.
  *result = context_string;
  int max_length = kMaxRandGenLen + result->length();

  // Advance state to configured initial state.
  int state = GetNextState(context_string, /*initial_state=*/-1);
  int chosen = -1;  // Initialize to non-zero to enter loop.
  while (success && chosen != 0 &&
         static_cast<int>(result->length()) < max_length) {
    std::vector<std::pair<double, int32>> prob_idx_pair_vector;
    double normalization;
    bool success = GetLMScores(/*context_string=*/"", state, &normalization,
                               &prob_idx_pair_vector);
    if (success) {
      const int pos = GetRandomPosition(prob_idx_pair_vector);
      GOOGLE_CHECK_GE(pos, 0);
      GOOGLE_CHECK_LT(pos, prob_idx_pair_vector.size());
      chosen = prob_idx_pair_vector[pos].second;
      if (chosen > 0) {
        // Only updates if not end-of-string.
        const std::string next_sym = utf8::EncodeUnicodeChar(chosen);
        *result += next_sym;
        state = GetNextState(next_sym, state);
      }
    } else {
      *result += "(subsequent generation failed)";
    }
  }
  if (success && static_cast<int>(result->length()) >= max_length) {
    *result += "(reached_length_limit)";
  }
  return success;
}

bool MozoLMClient::OneKbestSample(int k_best, const std::string& context_string,
                                  std::string* result) {
  std::vector<std::pair<double, int32>> prob_idx_pair_vector;
  double normalization;
  const bool success = GetLMScores(context_string, /*initial_state=*/-1,
                                   &normalization, &prob_idx_pair_vector);
  if (success) {
    *result = std::to_string(k_best) + "-best prob continuations:";
    for (int i = 0; i < k_best; i++) {
      // TODO: fix for general utf8 symbols.
      *result = absl::StrFormat("%s %c(%5.2f)", *result,
                                prob_idx_pair_vector[i].second,
                                prob_idx_pair_vector[i].first);
    }
  }
  return success;
}

MozoLMClient::MozoLMClient(const ClientServerConfig& grpc_config) {
  std::shared_ptr<::grpc::ChannelCredentials> creds;
  switch (grpc_config.credential_type()) {
    case ClientServerConfig::SSL:
      // TODO: setup SSL credentials.
      creds = ::grpc::InsecureChannelCredentials();
      break;
    case ClientServerConfig::INSECURE:
      creds = ::grpc::InsecureChannelCredentials();
      break;
    default:
      GOOGLE_LOG(ERROR) << "unknown credential type";
  }
  channel_ = ::grpc::CreateChannel(grpc_config.server_port(), creds);
  completion_client_ =
      absl::make_unique<MozoLMClientAsyncImpl>(MozoLMServer::NewStub(channel_));
  timeout_ = grpc_config.client_config().timeout();
}

}  // namespace grpc
}  // namespace mozolm
