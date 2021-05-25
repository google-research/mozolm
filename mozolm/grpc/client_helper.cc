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

#include <cmath>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "mozolm/stubs/logging.h"
#include "include/grpcpp/create_channel.h"
#include "include/grpcpp/grpcpp.h"
#include "include/grpcpp/security/credentials.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/notification.h"
#include "mozolm/grpc/client_async_impl.h"
#include "mozolm/grpc/server_config.pb.h"
#include "mozolm/grpc/server_helper.h"
#include "mozolm/utils/utf8_util.h"
#include "mozolm/stubs/status_macros.h"

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
  return -std::log2(prob);
}

// Client credentials factory: Configures SSL, if requested, otherwise uses an
// insecure channel.
std::shared_ptr<::grpc::ChannelCredentials>
BuildChannelCredentials(const ClientConfig &config,
                        ::grpc::ChannelArguments *channel_args) {
  if (config.server().auth().credential_type() == CREDENTIAL_TLS) {
    if (config.server().auth().has_tls()) {
      const std::string &server_cert =
          config.server().auth().tls().server_cert();
      ::grpc::SslCredentialsOptions ssl_options;
      ssl_options.pem_root_certs = server_cert;

      if (config.auth().has_tls()) {
        const auto &client_tls = config.auth().tls();
        if (!client_tls.target_name_override().empty()) {
          channel_args->SetSslTargetNameOverride(
              client_tls.target_name_override());
        }
        // Required for mutual authentication, if configured.
        ssl_options.pem_cert_chain = client_tls.client_cert();
        ssl_options.pem_private_key = client_tls.client_key();
      }
      return ::grpc::SslCredentials(ssl_options);
    } else {
      GOOGLE_LOG(WARNING) << "Secure credentials requested but no configuration found";
    }
  }
  GOOGLE_LOG(WARNING) << "Using insecure server credentials";
  return ::grpc::InsecureChannelCredentials();
}

}  // namespace

absl::Status ClientHelper::GetLMScores(
    const std::string& context_string, int initial_state, double* normalization,
    std::vector<std::pair<double, std::string>>* prob_idx_pair_vector) {
  if (completion_client_ == nullptr) {
    return absl::InternalError("Completion client not initialized");
  }
  RETURN_IF_ERROR(completion_client_->GetLMScore(context_string, initial_state,
                                                 timeout_sec_, normalization,
                                                 prob_idx_pair_vector));
  if (*normalization <= 0) {
    return absl::InternalError(absl::StrCat(
        "Invalid normalization factor: ", *normalization));
  } else {
    return absl::OkStatus();
  }
}

absl::StatusOr<int64> ClientHelper::GetNextState(
    const std::string& context_string,
    int initial_state) {
  int64 next_state;
  GOOGLE_CHECK_NE(completion_client_, nullptr);
  const absl::Status status = completion_client_->GetNextState(
      context_string, initial_state, timeout_sec_, &next_state);
  if (!status.ok()) {
    return absl::InternalError(absl::StrCat(
        "Getting next state failed for initial state ", initial_state,
        " in context \"", context_string, "\": ", status.ToString()));
  }
  return next_state;
}

absl::Status ClientHelper::UpdateCountGetDestStateScore(
    const std::string& context_string, int initial_state, int32 count,
    int64* next_state, double* normalization,
    std::vector<std::pair<double, std::string>>* prob_idx_pair_vector) {
  if (completion_client_ == nullptr) {
    return absl::InternalError("Completion client not initialized");
  }
  return completion_client_->UpdateCountGetDestStateScore(
      context_string, initial_state, timeout_sec_, count, next_state,
      normalization, prob_idx_pair_vector);
}

absl::Status ClientHelper::RandGen(const std::string& context_string,
                                   std::string* result) {
  // The context string is the prefix to the randomly generated string.
  *result = context_string;
  const int max_length = kMaxRandGenLen + result->length();

  // Advance state to configured initial state.
  const auto state_status = GetNextState(context_string, /*initial_state=*/-1);
  if (!state_status.ok()) return state_status.status();
  int64 state = state_status.value();
  std::string chosen;
  std::vector<std::pair<double, std::string>> prob_idx_pair_vector;
  double normalization;
  RETURN_IF_ERROR(GetLMScores(/*context_string=*/"", state, &normalization,
                              &prob_idx_pair_vector));
  bool success = true;
  do {
    if (success) {
      const int pos = GetRandomPosition(prob_idx_pair_vector);
      if (pos < 0 || pos >= prob_idx_pair_vector.size()) {
        return absl::InternalError(absl::StrCat("Invalid position: ", pos));
      }
      chosen = prob_idx_pair_vector[pos].second;
      if (!chosen.empty()) {
        // Only updates if not end-of-string (by convention, empty string).
        *result += chosen;
        prob_idx_pair_vector.clear();
        success =
            UpdateCountGetDestStateScore(chosen, state, /*count=*/1, &state,
                                         &normalization,
                                         &prob_idx_pair_vector).ok();
      }
    } else {
      *result += "(subsequent generation failed)";
    }
  } while (success && !chosen.empty() &&
           static_cast<int>(result->length()) < max_length);
  if (success && static_cast<int>(result->length()) >= max_length) {
    *result += "(reached_length_limit)";
  }
  if (!success) {
    return absl::InternalError("Count update failed");
  } else {
    return absl::OkStatus();
  }
}

absl::Status ClientHelper::OneKbestSample(int k_best,
                                          const std::string& context_string,
                                          std::string* result) {
  std::vector<std::pair<double, std::string>> prob_idx_pair_vector;
  double normalization;
  RETURN_IF_ERROR(GetLMScores(context_string, /*initial_state=*/-1,
                              &normalization, &prob_idx_pair_vector));
  *result = std::to_string(k_best) + "-best prob continuations:";
  for (int i = 0; i < k_best; i++) {
    *result = absl::StrFormat("%s %s(%5.3f)", *result,
                              prob_idx_pair_vector[i].second,
                              prob_idx_pair_vector[i].first);
  }
  return absl::OkStatus();
}

absl::Status ClientHelper::CalcBitsPerCharacter(const std::string& test_file,
                                                std::string* result) {
  std::ifstream infile(test_file);
  if (!infile.is_open()) {
    return absl::NotFoundError("Test file could not be accessed");
  }
  absl::Status status = absl::OkStatus();
  std::string input_line;
  int tot_chars = 0;
  int tot_oov_chars = 0;
  double tot_bits = 0.0;
  double unused_normalization;
  while (status.ok() && std::getline(infile, input_line)) {
    std::vector<std::string> input_chars = utf8::StrSplitByChar(input_line);
    input_chars.push_back("");  // End-of-string character.
    int64 state = 0;  // Will start at initial state of the model.
    std::vector<std::pair<double, std::string>> prob_idx_pair_vector;
    RETURN_IF_ERROR(GetLMScores(/*context_string=*/"", state,
                                &unused_normalization, &prob_idx_pair_vector));
    for (const auto& utf8_sym : input_chars) {
      const int idx = FindStringIndex(prob_idx_pair_vector, utf8_sym);
      tot_bits += CalculateBits(idx, prob_idx_pair_vector);
      if (idx < 0) {
         ++tot_oov_chars;
      }
      ++tot_chars;
      prob_idx_pair_vector.clear();
      status = UpdateCountGetDestStateScore(utf8_sym, state, /*count=*/1,
                                            &state, &unused_normalization,
                                            &prob_idx_pair_vector);
      if (!status.ok()) break;
    }
  }
  if (infile.is_open()) {
    infile.close();
  }
  if (!status.ok()) return status;
  *result = absl::StrJoin(
      std::make_tuple("Total characters: ", tot_chars, " (", tot_oov_chars,
                      " OOV); bits per character: ",
                      tot_bits / static_cast<double>(tot_chars)), "");
  return absl::OkStatus();
}

absl::Status ClientHelper::Init(const ClientConfig& config) {
  ::grpc::ChannelArguments channel_args;
  std::shared_ptr<::grpc::ChannelCredentials> creds = BuildChannelCredentials(
      config, &channel_args);
  if (creds == nullptr) {
    return absl::InternalError("Failed to build channel credentials");
  }
  channel_ = ::grpc::CreateCustomChannel(config.server().address_uri(), creds,
                                         channel_args);
  completion_client_ =
      absl::make_unique<ClientAsyncImpl>(MozoLMService::NewStub(channel_));
  timeout_sec_ = config.timeout_sec();
  return absl::OkStatus();
}

void InitConfigDefaults(ClientConfig* config) {
  InitConfigDefaults(config->mutable_server());
  if (config->timeout_sec() <= 0.0) {
    config->set_timeout_sec(kDefaultClientTimeoutSec);
  }
}

absl::Status RunClient(const ClientConfig& config) {
  ClientHelper client;
  RETURN_IF_ERROR(client.Init(config));

  absl::Status status;
  std::string result;
  switch (config.request_type()) {
    case ClientConfig::RANDGEN:
      status = client.RandGen(config.context_string(), &result);
      break;
    case ClientConfig::K_BEST_ITEMS:
      status = client.OneKbestSample(config.k_best(), config.context_string(),
                                     &result);
      break;
    case ClientConfig::BITS_PER_CHAR_CALCULATION:
      status = client.CalcBitsPerCharacter(config.test_corpus(), &result);
      break;
    default:
      return absl::InvalidArgumentError("Unknown client request type");
  }
  if (status.ok()) {
    absl::PrintF("%s\n", result);
  }
  return status;
}

}  // namespace grpc
}  // namespace mozolm
