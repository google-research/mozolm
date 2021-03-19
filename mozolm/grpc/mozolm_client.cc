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

#include <fstream>
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
#include "absl/strings/str_join.h"
#include "absl/synchronization/notification.h"
#include "mozolm/grpc/grpc_util.pb.h"
#include "mozolm/grpc/mozolm_client_async_impl.h"
#include "mozolm/utils/utf8_util.h"

namespace mozolm {
namespace grpc {
namespace {

constexpr int kNumCodepoints = 143859;   // Total possible Unicode codepoints.
constexpr float kMixEpsilon = 0.00000001;  // Amount to weight uniform prob.

// Returns random probability threshold between 0 and 1.
double GetUniformThreshold() {
  absl::BitGen gen;
  return absl::Uniform(gen, 0, 100000) / static_cast<double>(100000);
}

// Uses random number to choose position according to returned distribution.
int GetRandomPosition(
    const std::vector<std::pair<double, std::string>>& prob_idx_pair_vector) {
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

// Returns the index for the utf8_sym in the given vector, -1 if not found.
// Since the vector is in descending probability order, will use linear scan to
// find match, since this will on average be efficient.
int FindStringIndex(
    const std::vector<std::pair<double, std::string>>& prob_idx_pair_vector,
    const std::string& utf8_sym) {
  for (int idx = 0; idx < prob_idx_pair_vector.size(); ++idx) {
    if (utf8_sym == prob_idx_pair_vector[idx].second) {
      return idx;
    }
  }
  return -1;
}

// Returns a uniform codepoint probability weighted by epsilon mix parameter.
double UniformMixValue() {
  return static_cast<double>(kMixEpsilon) / static_cast<double>(kNumCodepoints);
}

// Finds value in vector and returns bits.  Mixes with a uniform to ensure full
// coverage, i.e., probability = (1-\epsilon) P + \epsilon U, where P is the
// model probability and U is a uniform distribution over unicode codepoints.
double CalculateBits(
    int idx,
    const std::vector<std::pair<double, std::string>>& prob_idx_pair_vector) {
  double prob = UniformMixValue();
  if (idx >= 0) {
    prob += prob_idx_pair_vector[idx].first * (1.0 - kMixEpsilon);
  }
  return -log2(prob);
}

}  // namespace

bool MozoLMClient::GetLMScores(
    const std::string& context_string, int initial_state, double* normalization,
    std::vector<std::pair<double, std::string>>* prob_idx_pair_vector) {
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
  // The context string is the prefix to the randomly generated string.
  *result = context_string;
  int max_length = kMaxRandGenLen + result->length();

  // Advance state to configured initial state.
  int state = GetNextState(context_string, /*initial_state=*/-1);
  bool success;
  std::string chosen;
  do {
    std::vector<std::pair<double, std::string>> prob_idx_pair_vector;
    double normalization;
    success = GetLMScores(/*context_string=*/"", state, &normalization,
                          &prob_idx_pair_vector);
    if (success) {
      const int pos = GetRandomPosition(prob_idx_pair_vector);
      GOOGLE_CHECK_GE(pos, 0);
      GOOGLE_CHECK_LT(pos, prob_idx_pair_vector.size());
      chosen = prob_idx_pair_vector[pos].second;
      if (!chosen.empty()) {
        // Only updates if not end-of-string (by convention, empty string).
        *result += chosen;
        state = GetNextState(chosen, state);
      }
    } else {
      *result += "(subsequent generation failed)";
    }
  } while (success && !chosen.empty() &&
           static_cast<int>(result->length()) < max_length);
  if (success && static_cast<int>(result->length()) >= max_length) {
    *result += "(reached_length_limit)";
  }
  return success;
}

bool MozoLMClient::OneKbestSample(int k_best, const std::string& context_string,
                                  std::string* result) {
  std::vector<std::pair<double, std::string>> prob_idx_pair_vector;
  double normalization;
  const bool success = GetLMScores(context_string, /*initial_state=*/-1,
                                   &normalization, &prob_idx_pair_vector);
  if (success) {
    *result = std::to_string(k_best) + "-best prob continuations:";
    for (int i = 0; i < k_best; i++) {
      *result = absl::StrFormat("%s %s(%5.3f)", *result,
                                prob_idx_pair_vector[i].second,
                                prob_idx_pair_vector[i].first);
    }
  }
  return success;
}

bool MozoLMClient::CalcBitsPerCharacter(const std::string& test_file,
                                        std::string* result) {
  bool success = true;
  std::ifstream infile(test_file);
  if (!infile.is_open()) {
    success = false;  // Test file could not be accessed.
  }
  std::string input_line;
  int tot_chars = 0;
  int tot_oov_chars = 0;
  double tot_bits = 0.0;
  double unused_normalization;
  while (success && std::getline(infile, input_line)) {
    std::vector<std::string> input_chars = utf8::StrSplitByChar(input_line);
    int state = -1;  // Will start at initial state of the model.
    for (const auto& utf8_sym : input_chars) {
      std::vector<std::pair<double, std::string>> prob_idx_pair_vector;
      success = GetLMScores(/*context_string=*/"", state, &unused_normalization,
                            &prob_idx_pair_vector);
      if (!success) break;
      int idx = FindStringIndex(prob_idx_pair_vector, utf8_sym);
      if (idx < 0) {
        GOOGLE_LOG(WARNING) << "OOV symbol found: " << utf8_sym;
        ++tot_oov_chars;
      }
      ++tot_chars;
      tot_bits += CalculateBits(idx, prob_idx_pair_vector);
      state = GetNextState(utf8_sym, state);
    }
    std::vector<std::pair<double, std::string>> prob_idx_pair_vector;
    success = GetLMScores(/*context_string=*/"", state, &unused_normalization,
                          &prob_idx_pair_vector);
    if (success) {
      int idx = FindStringIndex(prob_idx_pair_vector, "");
      ++tot_chars;  // Adds in end-of-string character.
      tot_bits += CalculateBits(idx, prob_idx_pair_vector);
    }
  }
  if (infile.is_open()) {
    infile.close();
  }
  if (success) {
    *result = absl::StrJoin(
        std::make_tuple("Total characters: ", tot_chars, " (", tot_oov_chars,
                        " OOV); bits per character: ",
                        tot_bits / static_cast<double>(tot_chars)),
        "");
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
