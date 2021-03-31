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

#ifndef MOZOLM_MOZOLM_MODELS_LANGUAGE_MODEL_HUB_H_
#define MOZOLM_MOZOLM_MODELS_LANGUAGE_MODEL_HUB_H_

#include <memory>
#include <string>
#include <vector>

#include "mozolm/stubs/integral_types.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mozolm/models/language_model.h"
#include "mozolm/models/lm_scores.pb.h"
#include "mozolm/models/model_config.pb.h"

namespace mozolm {
namespace models {

class LanguageModelHubState {
 public:
  LanguageModelHubState() = default;

  // Initializes given a vector of states, default values used for start state.
  explicit LanguageModelHubState(const std::vector<int>& model_states,
                                 int prev_state = -1, int state_sym = 0)
      : model_states_(model_states),
        prev_state_(prev_state),
        state_sym_(state_sym) {}

  ~LanguageModelHubState() = default;

  int ModelStateSize() const { return model_states_.size(); }
  int state_sym() const { return state_sym_; }
  int prev_state() const { return prev_state_; }

  // Returns existing next state for symbol if exists; -1 otherwise.
  int next_state(int utf8_sym) const {
    return next_states_.contains(utf8_sym) ? next_states_.find(utf8_sym)->second
                                           : -1;
  }

  // Returns model state for index within range; -1 otherwise.
  int model_state(int idx) const {
    return idx >= 0 && idx < model_states_.size() ? model_states_[idx] : -1;
  }

  // Adds next state to next_states_.
  void AddNextState(int utf8_sym, int next_state) {
    next_states_.insert({utf8_sym, next_state});
  }

  // Resets values with those from given hub_state.
  absl::StatusOr<std::vector<int>> UpdateHubState(
      const LanguageModelHubState& hub_state);

  // Resets previous state when previous state has been overwritten.
  void ResetPrevState() {
    prev_state_ = -1;
  }

 private:
  std::vector<int> model_states_;  // Stores state IDs from component models.
  std::vector<std::string>
      model_state_prefixes_;  // Stores word prefixes at state in models.
  int prev_state_;            // Previous state in the model hub.
  absl::flat_hash_map<int, int> next_states_;  // Set of next states.
  int state_sym_;             // Last symbol leading to this state.
};

// TODO: Initialize with a desired target alphabet.
class LanguageModelHub {
 public:
  LanguageModelHub() = default;
  ~LanguageModelHub() = default;

  // Adds language model to collection of models.
  void AddModel(std::unique_ptr<LanguageModel> language_model) {
    language_models_.push_back(std::move(language_model));
  }

  // Initializes set of models after all models have been added.
  bool InitializeModels(const ModelHubConfig &config);

  // Provides the last symbol to reach the state.
  int StateSym(int state);

  // Provides the state reached from state following utf8_sym.
  int NextState(int state, int utf8_sym);

  // Provides the state reached from the init_state after consuming the context
  // string. If string is empty, returns the init_state.  If init_state is less
  // than zero, the model will start at the start state of the model.
  int ContextState(const std::string& context = "", int init_state = -1);

  // Copies the probs and normalization from the given state into the response.
  bool ExtractLMScores(int state, LMScores* response);

  // Updates the count for the utf8_syms at the current state.
  bool UpdateLMCounts(int32 state, const std::vector<int>& utf8_syms,
                      int64 count);

 private:
  // Determines vector index for new state, creates state and returns index.
  absl::StatusOr<int> AssignNewHubState(const std::vector<int>& model_states,
                                        int prev_state, int state_sym);

  // Updates already allocated hub state to new information.
  absl::Status UpdateHubState(int idx, const std::vector<int>& model_states,
                              int prev_state, int state_sym);

  // Initializes already allocated start hub state with start states.
  absl::Status InitializeStartHubState();

  // States in the model hub, tracking states in all component models.
  std::vector<std::unique_ptr<LanguageModelHubState>> hub_states_;
  int last_created_hub_state_;  // Tracks which hub states recently created.
  int max_hub_states_;          // Maximum number of hub states to allow.
  std::vector<double> mixture_weights_;  // Weight for each model in mixture.
  std::vector<std::unique_ptr<LanguageModel>> language_models_;
};

}  // namespace models
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_MODELS_LANGUAGE_MODEL_HUB_H_
