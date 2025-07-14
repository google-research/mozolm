// Copyright 2025 MozoLM Authors.
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

#include "mozolm/models/language_model.h"

#include <algorithm>

#include "ngram/ngram-model.h"
#include "absl/strings/str_cat.h"
#include "nisaba/port/utf8_util.h"

namespace mozolm {
namespace models {

int LanguageModel::ContextState(const std::string& context, int init_state) {
  int this_state = init_state < 0 ? start_state_ : init_state;
  if (!context.empty()) {
    const std::vector<int> context_utf8 =
        nisaba::utf8::StrSplitByCharToUnicode(context);
    for (auto sym : context_utf8) {
      this_state = NextState(this_state, sym);
      if (this_state < 0) {
        // Returns to start state if symbol not found.
        // TODO: should it return to a null context state?
        this_state = start_state_;
      }
    }
  }
  return this_state;
}

absl::StatusOr<std::vector<std::pair<double, std::string>>>
GetTopHypotheses(const LMScores &scores, int top_n) {
  const int num_entries = scores.probabilities().size();
  if (num_entries != scores.symbols().size()) {
    return absl::InternalError(absl::StrCat(
        "Mismatching number of probabilities (", num_entries,
        ") and symbols (", scores.symbols().size(), ")"));
  }
  if (num_entries <= top_n) {
    return absl::InternalError(absl::StrCat(
        "Too many candidates requested: ", top_n));
  } else if (num_entries == 0) {
    return absl::InternalError("No scores to return");
  }
  std::vector<std::pair<double, std::string>> hyps;
  hyps.reserve(num_entries);
  for (int i = 0; i < num_entries; ++i) {
    hyps.push_back({ scores.probabilities(i), scores.symbols(i) });
  }
  std::sort(hyps.begin(), hyps.end(),
            [](const std::pair<double, std::string> &a,
               const std::pair<double, std::string> &b) {
              return a.first > b.first;
            });
  if (top_n > 0) {
    hyps = std::vector(hyps.begin(), hyps.begin() + top_n);
  }
  return std::move(hyps);
}

void SoftmaxRenormalize(std::vector<double> *neg_log_probs) {
  double tot_prob = (*neg_log_probs)[0];
  double kahan_factor = 0.0;
  for (int i = 1; i < neg_log_probs->size(); ++i) {
    tot_prob =
        ngram::NegLogSum(tot_prob, (*neg_log_probs)[i], &kahan_factor);
  }
  for (int i = 0; i < neg_log_probs->size(); ++i) {
    (*neg_log_probs)[i] -= tot_prob;
  }
}

}  // namespace models
}  // namespace mozolm
