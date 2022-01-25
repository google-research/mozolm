// Copyright 2022 MozoLM Authors.
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

#ifndef MOZOLM_MOZOLM_MODELS_LANGUAGE_MODEL_H_
#define MOZOLM_MOZOLM_MODELS_LANGUAGE_MODEL_H_

#include <string>
#include <utility>
#include <vector>

#include "mozolm/stubs/integral_types.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mozolm/models/lm_scores.pb.h"
#include "mozolm/models/model_storage.pb.h"

namespace mozolm {
namespace models {

class LanguageModel {
 public:
  virtual ~LanguageModel() = default;

  // Reads the model from the model storage.
  virtual absl::Status Read(const ModelStorage &storage) = 0;

  // Provides the last symbol to reach the state.
  virtual int StateSym(int state) {
    return -1;  // Requires a derived class to complete.
  }

  // Provides the state reached from state following utf8_sym.
  virtual int NextState(int state, int utf8_sym) {
     return -1;  // Requires a derived class to complete.
  }

  // Provides the state reached from the init_state after consuming the context
  // string. If string is empty, returns the init_state.  If init_state is less
  // than zero, the model will start at the start state of the model.
  int ContextState(const std::string &context = "", int init_state = -1);

  int start_state() const { return start_state_; }

  // Copies the probs and normalization from the given state into the response.
  virtual bool ExtractLMScores(int state, LMScores* response) {
    return false;  // Requires a derived class to complete.
  }

  // Tries to write the Fst representation if it exists in the derived class.
  virtual absl::Status WriteFst(const std::string &ofile) const {
    // Requires a derived class to complete.
    return absl::UnimplementedError(
        "No FST writing defined for this derived class");
  }

  // Returns the negative log probability of the utf8_sym at the state.
  virtual double SymLMScore(int state, int utf8_sym) {
    return -log(0.0);  // Requires a derived class to complete.
  }

  // Updates the count for the utf8_syms at the current state.
  virtual bool UpdateLMCounts(int32 state, const std::vector<int>& utf8_syms,
                              int64 count) {
    return false;  // Requires a derived class to complete.
  }

  // Returns true if model is static, false if model is dynamic.
  virtual bool IsStatic() const { return true; }

 protected:
  LanguageModel() : start_state_(0) {}

  // Allows derived classes to set the start state of the model.
  void set_start_state(int32 state) { start_state_ = state; }

 private:
  int32 start_state_;
};

// Given the protocol buffer containing the language model scores and the
// corresponding vocabulary returns the sorted list of tuples containing the
// top requested hypotheses. If `top_n` is negative (default) returns all
// hypotheses, otherwise returns the most likely `top_n`.
absl::StatusOr<std::vector<std::pair<double, std::string>>> GetTopHypotheses(
    const LMScores &scores, int top_n = -1);

// Renormalizes negative log probabilities over vector.
void SoftmaxRenormalize(std::vector<double> *neg_log_probs);

}  // namespace models
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_MODELS_LANGUAGE_MODEL_H_
