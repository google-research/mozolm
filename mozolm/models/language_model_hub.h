// Copyright 2023 MozoLM Authors.
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

  // Initializes given a vector of states, default values used for start state,
  // plus allocates vectors for Bayesian mixtures if needed.
  explicit LanguageModelHubState(const std::vector<int>& model_states,
                                 int prev_state = -1, int state_sym = 0,
                                 int bayesian_history_length = 0)
      : model_states_(model_states),
        prev_state_(prev_state),
        state_sym_(state_sym) {
    if (bayesian_history_length > 0) {
      InitBayesianHistory(bayesian_history_length);
    }
  }

  ~LanguageModelHubState() = default;

  int ModelStateSize() const { return model_states_.size(); }
  int state_sym() const { return state_sym_; }
  int prev_state() const { return prev_state_; }
  absl::flat_hash_map<int, int> next_states() const {
    return next_states_;
  }
  std::vector<std::vector<double>> bayesian_history_probs() const {
    return bayesian_history_probs_;
  }
  std::vector<double> bayesian_history_probs_sum() const {
    return bayesian_history_probs_sum_;
  }

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

  // For a given hub state, this verifies model state information which may have
  // changed due to count updates. Returns false if base information is wrong.
  bool VerifyOrCorrectModelStates(int prev_state, int utf8_sym,
                                  const std::vector<int>& model_states);

  // Updates the Bayesian history probabilities at the state.
  void UpdateBayesianHistory(
      const std::vector<double>& lm_probs,
      const std::vector<std::vector<double>>& prev_probs);

  // Resets previous state when previous state has been overwritten.
  void ResetPrevState() {
    prev_state_ = -1;
  }

 private:
  // Initializes Bayesian history probabilities if needed.
  void InitBayesianHistory(int bayesian_history_length);

  std::vector<int> model_states_;  // Stores state IDs from component models.
  std::vector<std::string>
      model_state_prefixes_;  // Stores word prefixes at state in models.
  int prev_state_;            // Previous state in the model hub.
  absl::flat_hash_map<int, int> next_states_;  // Set of next states.
  int state_sym_;             // Last symbol leading to this state.

  // Holds the (negative log) probabilities of recent symbols for calculating
  // Bayesian interpolation model mixing parameters. Empty if not using Bayesian
  // methods.
  std::vector<std::vector<double>> bayesian_history_probs_;
  std::vector<double> bayesian_history_probs_sum_;  // Holds pre-summed value.
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
  absl::Status InitializeModels(const ModelHubConfig &config);

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

  // Updates probabilities from each model to allow Bayesian interpolation.
  void UpdateBayesianHistory(int32 state);

  // Bayesian interpolation methods are based on a generalization of methods
  // shown in Allauzen and Riley (2011) "Bayesian language model interpolation
  // for mobile speech input." Given K models, each k \in K having a normalized
  // prior weight w_k such that \sum_{k \in K} w_k = 1.0, then
  // P(w | h) = \sum_{k \in K} m_k(h) p_k(w | h), where p_k(w | h) is the
  // probability of w given h in model k, and m_k(h) is the mixture weight for
  // history h, calculated as:
  // m_k(h) = w_k p_k(h) / ( sum_{l \in K} w_l p_l(h) ).  In this version, the
  // length of the history considered when calculating p(h | k) is
  // parameterized, so that we consider only the previous j symbols regardless
  // of the order of the model, where j is the bayesian_history_length parameter
  // in the ModelHubConfig proto. If that parameter is set to less than one,
  // then standard interpolation is used, i.e., just based on the prior weight
  // w_k. At character c_i, let the previous history be denoted h_i = c_0...
  // c_{i-1}. Then, if bayesian_history_length = j > 0:
  // m_k(h_i) = w_k p_k(c_{i-1} | h_{i-1}) ... p_k(c_{i-j} | h_{i-j}) / Z, where
  // Z is the appropriate normalization across all models.
  // One special note about the use of this with dynamic models. This method
  // provides more weight to models that have assigned higher probability to the
  // symbols in the history. For this reason, the history probabilities used to
  // calculate the mixture should be based on probabilities before a dynamic
  // model's counts are updated for the current instance. Otherwise, it will
  // inflate the probabilities that the model has been providing for the history
  // and over-rely on that model for the next estimate.  For this reason, the
  // Bayesian histories are updated prior to model counts being updated.
  std::vector<double> GetBayesianMixtureWeights(int state) const;

  // Calculates normalized mixture weights.  If anything other than Bayesian
  // methods, no special calculation required.
  std::vector<double> GetMixtureWeights(int state, bool result) const;

  // Verifies model states after updating counts, and corrects if they differ.
  bool VerifyOrCorrectModelStates(int32 state,
                                  const std::vector<int>& utf8_syms);

  // States in the model hub, tracking states in all component models.
  std::vector<std::unique_ptr<LanguageModelHubState>> hub_states_;
  int last_created_hub_state_;  // Tracks which hub states recently created.
  int max_hub_states_;          // Maximum number of hub states to allow.
  std::vector<double> mixture_weights_;  // Weight for each model in mixture.

  int bayesian_history_length_;  // Length of history for Bayesian mixing.

  std::vector<std::unique_ptr<LanguageModel>> language_models_;
};

}  // namespace models
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_MODELS_LANGUAGE_MODEL_HUB_H_
