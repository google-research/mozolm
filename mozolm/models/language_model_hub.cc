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

#include "mozolm/models/language_model_hub.h"

#include <algorithm>
#include <cmath>

#include "mozolm/stubs/logging.h"
#include "ngram/ngram-model.h"
#include "nisaba/port/utf8_util.h"
#include "nisaba/port/status_macros.h"

namespace mozolm {
namespace models {

constexpr int kMaxHubStates = 10000;  // Max number of hub states to maintain.

namespace impl {
namespace {

// Scans through lm_scores, adding each item to existing flat hash map, scaled
// with mix_weight and combined with already existing values for the string.
double MixResults(const LMScores& lm_scores, double mix_weight,
                  absl::flat_hash_map<std::string, double>* mixed_values) {
  for (int i = 0; i < lm_scores.probabilities_size(); i++) {
    const std::string key = lm_scores.symbols(i);
    double value = -std::log(lm_scores.probabilities(i)) + mix_weight;
    if (mixed_values->contains(key)) {
      value = ngram::NegLogSum(value, mixed_values->find(key)->second);
    }
    mixed_values->insert_or_assign(key, value);
  }
  // Weights the normalization value by the mixture weight.
  return lm_scores.normalization() * std::exp(-mix_weight);
}

void ExtractMixture(
    const absl::flat_hash_map<std::string, double>& mixed_values,
    double mixed_normalization, LMScores* response) {
  std::vector<std::pair<std::string, double>> values(mixed_values.size());
  int idx = 0;
  double norm;
  for (const auto& mixed_value : mixed_values) {
    if (idx == 0) {
      norm = mixed_value.second;
    } else {
      norm = ngram::NegLogSum(norm, mixed_value.second);
    }
    values[idx++] = std::make_pair(mixed_value.first, mixed_value.second);
  }
  // Sorts in lexicographic order, normalizes and converts from -log to probs.
  std::sort(values.begin(), values.end());
  response->mutable_symbols()->Reserve(values.size());
  response->mutable_probabilities()->Reserve(values.size());
  for (int i = 0; i < values.size(); ++i) {
    response->add_symbols(values[i].first);
    response->add_probabilities(std::exp(-values[i].second + norm));
  }
  response->set_normalization(mixed_normalization);
}

}  // namespace
}  // namespace impl

bool LanguageModelHub::InitializeModels(const ModelHubConfig& config) {
  mixture_weights_.clear();
  switch (config.mixture_type()) {
    case ModelHubConfig::NONE:
      // Only uses results from first model.
      mixture_weights_.push_back(0.0);
      break;
    case ModelHubConfig::INTERPOLATION:
      if (config.model_config_size() < 2) {
        // Either just 1 model in config, hence no mixing, or no models added in
        // config, so using default model (also no mixing).
        mixture_weights_.push_back(0.0);
      } else {
        double normalization;
        mixture_weights_.resize(config.model_config_size());
        for (auto idx = 0; idx < config.model_config_size(); ++idx) {
          // Stores negative log mixing weights for each model.
          mixture_weights_[idx] = config.model_config(idx).weight();
          if (idx == 0) {
            normalization = mixture_weights_[idx];
          } else {
            normalization =
                ngram::NegLogSum(normalization, mixture_weights_[idx]);
          }
        }
        for (auto idx = 0; idx < mixture_weights_.size(); ++idx) {
          // Scale the mixture weights by a constant to sum to 1.
          mixture_weights_[idx] -= normalization;
        }
      }
      break;
    default:
      return false;  // Unknown mixture type.
  }
  // Creates a start hub state, by convention index 0.
  hub_states_.clear();
  hub_states_.push_back(std::unique_ptr<LanguageModelHubState>());
  const std::vector<int> dummy_states(language_models_.size());
  hub_states_[0] = std::unique_ptr<LanguageModelHubState>(
      new LanguageModelHubState(dummy_states, -1, 0));
  if (InitializeStartHubState() != absl::OkStatus()) {
    return false;
  }
  if (config.maximim_maintained_states() < 10) {
    max_hub_states_ = kMaxHubStates;
  } else {
    max_hub_states_ = config.maximim_maintained_states();
  }
  last_created_hub_state_ = 0;
  return true;
}

int LanguageModelHub::StateSym(int state) {
  if (state < 0 || state >= hub_states_.size()) {
    return -1;
  }
  return hub_states_[state]->state_sym();
}

absl::Status LanguageModelHub::UpdateHubState(
    int idx, const std::vector<int>& model_states, int prev_state,
    int state_sym) {
  std::vector<int> old_next_states;
  ASSIGN_OR_RETURN(old_next_states,
                   hub_states_[idx]->UpdateHubState(LanguageModelHubState(
                       model_states, prev_state, state_sym)));
  for (auto next_state : old_next_states) {
    // Removes prev_state_ values that refer to old, overwritten hub state.
    hub_states_[next_state]->ResetPrevState();
  }
  return absl::OkStatus();
}

absl::Status LanguageModelHub::InitializeStartHubState() {
  std::vector<int> start_states(language_models_.size());
  for (auto idx = 0; idx < language_models_.size(); ++idx) {
    start_states[idx] = language_models_[idx]->start_state();
  }
  return UpdateHubState(0, start_states, -1, 0);
}

absl::StatusOr<int> LanguageModelHub::AssignNewHubState(
    const std::vector<int>& model_states, int prev_state, int state_sym) {
  int idx;
  if (hub_states_.size() >= max_hub_states_) {
    idx = last_created_hub_state_ + 1;
    if (idx >= max_hub_states_) {
      // Going back to overwrite earlier states, reinitializes start state.
      auto init_hub_state_status = InitializeStartHubState();
      if (init_hub_state_status != absl::OkStatus()) {
        return init_hub_state_status;
      }
      idx = 1;
    }
    auto update_hub_state =
        UpdateHubState(idx, model_states, prev_state, state_sym);
    if (update_hub_state != absl::OkStatus()) {
      return update_hub_state;
    }
  } else {
    idx = hub_states_.size();
    hub_states_.push_back(std::unique_ptr<LanguageModelHubState>());
    hub_states_[idx] = std::unique_ptr<LanguageModelHubState>(
        new LanguageModelHubState(model_states, prev_state, state_sym));
  }
  last_created_hub_state_ = idx;
  hub_states_[prev_state]->AddNextState(state_sym, idx);
  return idx;
}

int LanguageModelHub::NextState(int state, int utf8_sym) {
  if (state < 0 || state >= hub_states_.size()) {
    // Resets invalid state to the start state, by convention 0.
    state = 0;
  }
  int next_state = hub_states_[state]->next_state(utf8_sym);
  if (next_state >= 0) {
    // Already created next state for that symbol.
    return next_state;
  }
  std::vector<int> next_states(language_models_.size());
  for (auto idx = 0; idx < language_models_.size(); ++idx) {
    next_states[idx] = language_models_[idx]->NextState(
        hub_states_[state]->model_state(idx), utf8_sym);
  }
  auto new_state_status = AssignNewHubState(next_states, state, utf8_sym);
  if (new_state_status.ok()) {
    return new_state_status.value();
  }
  return 0;  // Returns start state (0) if fails to assign new hub state.
}

int LanguageModelHub::ContextState(const std::string& context, int init_state) {
  // Sets initial state to start state if not otherwise valid.
  int this_state = init_state < 0 ? 0 : init_state;
  if (!context.empty()) {
    const std::vector<int> context_utf8 =
        nisaba::utf8::StrSplitByCharToUnicode(context);
    for (const auto& utf8_code : context_utf8) {
      this_state = NextState(this_state, utf8_code);
      if (this_state < 0) {
        // Returns to start state if symbol not found.
        // TODO: should it return to a null context state?
        this_state = 0;
      }
    }
  }
  return this_state;
}

bool LanguageModelHub::ExtractLMScores(int state, LMScores* response) {
  bool result = state >= 0 && state < hub_states_.size();
  int idx = 0;
  if (result && mixture_weights_.size() < 2) {
    // Returns from first model as no mixing is required.
    return language_models_[idx]->ExtractLMScores(
        hub_states_[state]->model_state(idx), response);
  }
  absl::flat_hash_map<std::string, double> mixed_values;
  double mixed_normalization = 0;
  while (result && idx < mixture_weights_.size()) {
    LMScores this_response;
    result = language_models_[idx]->ExtractLMScores(
        hub_states_[state]->model_state(idx), &this_response);
    if (result) {
      mixed_normalization +=
          impl::MixResults(this_response, mixture_weights_[idx], &mixed_values);
    }
    ++idx;
  }
  if (result) {
    impl::ExtractMixture(mixed_values, mixed_normalization, response);
  }
  return result;
}

bool LanguageModelHub::UpdateLMCounts(int32 state,
                                      const std::vector<int>& utf8_syms,
                                      int64 count) {
  bool result = state >= 0 && state < hub_states_.size();
  int idx = 0;
  while (result && idx < mixture_weights_.size()) {
    result = language_models_[idx]->UpdateLMCounts(
        hub_states_[state]->model_state(idx), utf8_syms, count);
    ++idx;
  }
  if (result) {
    result = VerifyOrCorrectModelStates(state, utf8_syms);
  }
  return result;
}

bool LanguageModelHub::VerifyOrCorrectModelStates(
    int32 state, const std::vector<int>& utf8_syms) {
  for (int utf8_sym : utf8_syms) {
    // Checks for next state; if there, verifies (and updates if needed) model
    // state information.
    int next_state = hub_states_[state]->next_state(utf8_sym);
    bool result;
    if (next_state >= 0) {
      // Collects model next state vector to double check.
      std::vector<int> next_states(language_models_.size());
      for (auto idx = 0; idx < next_states.size(); ++idx) {
        next_states[idx] = language_models_[idx]->NextState(
            hub_states_[state]->model_state(idx), utf8_sym);
      }
      result = hub_states_[next_state]->VerifyOrCorrectModelStates(
          state, utf8_sym, next_states);
    } else {
      // Returns true, since new states will be created anyhow for all
      // continuations from this point.
      return true;
    }
    if (!result) {
      return false;
    }
    state = next_state;
  }
  return true;
}

bool LanguageModelHubState::VerifyOrCorrectModelStates(
    int prev_state, int utf8_sym, const std::vector<int>& model_states) {
  if (prev_state_ != prev_state || state_sym_ != utf8_sym) {
    return false;
  }
  for (auto idx = 0; idx < model_states.size(); ++idx) {
    model_states_[idx] = model_states[idx];
  }
  return true;
}

absl::StatusOr<std::vector<int>> LanguageModelHubState::UpdateHubState(
    const LanguageModelHubState& hub_state) {
  if (model_states_.size() != hub_state.ModelStateSize()) {
    return absl::InternalError("Size difference between hub state and models.");
  }
  std::vector<int> old_next_states(next_states_.size());
  int idx = 0;
  for (auto next_state : next_states_) {
    old_next_states[idx++] = next_state.second;
  }
  next_states_.clear();
  for (int i = 0; i < model_states_.size(); ++i) {
    model_states_[i] = hub_state.model_state(i);
  }
  prev_state_ = hub_state.prev_state();
  state_sym_ = hub_state.state_sym();
  return std::move(old_next_states);
}

}  // namespace models
}  // namespace mozolm
