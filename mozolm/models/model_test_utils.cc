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

#include "mozolm/models/model_test_utils.h"

#include "gtest/gtest.h"
#include "mozolm/models/lm_scores.pb.h"

namespace mozolm {
namespace models {

void CheckTopCandidateForContext(const std::string &context,
                                 LanguageModel *model,
                                 std::pair<double, std::string> *candidate) {
  const int state = model->ContextState(context);
  EXPECT_LT(0, state) << "Invalid state: " << state;
  LMScores result;
  EXPECT_TRUE(model->ExtractLMScores(state, &result))
      << "Failed to extract scores for context: " << context;
  EXPECT_LT(0, result.normalization())
      << "Invalid normalization factor: " << result.normalization();
  const auto scores_status = GetTopHypotheses(result, /* top_n= */1);
  ASSERT_TRUE(scores_status.ok())
      << "Failed to extract scores: " << scores_status.status().ToString();
  const auto scores = std::move(scores_status.value());
  *candidate = std::move(scores[0]);
  EXPECT_LT(0.0, candidate->first) << "Invalid log prob: " << candidate->first;
}

}  // namespace models
}  // namespace mozolm
