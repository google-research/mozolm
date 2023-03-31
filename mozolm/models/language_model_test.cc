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

#include "mozolm/models/language_model.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
#include <vector>

#include "gmock/gmock.h"
#include "nisaba/port/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "mozolm/models/lm_scores.pb.h"

namespace mozolm {
namespace models {
namespace {

TEST(LanguageModelTest, CheckGetTopHypotheses) {
  LMScores scores_proto;
  auto scores_status = GetTopHypotheses(scores_proto);
  EXPECT_FALSE(scores_status.ok());  // No scores.
  scores_proto.add_probabilities(0.0);

  scores_status = GetTopHypotheses(scores_proto);
  EXPECT_FALSE(scores_status.ok());  // Symbols not set.
  scores_proto.add_symbols("a");
  scores_proto.add_probabilities(0.3);
  scores_proto.add_symbols("b");
  scores_proto.add_probabilities(0.6);
  scores_proto.add_symbols("c");

  scores_status = GetTopHypotheses(scores_proto, 10);
  EXPECT_FALSE(scores_status.ok());  // Too many requested..

  scores_status = GetTopHypotheses(scores_proto);
  EXPECT_TRUE(scores_status.ok());
  auto scores = std::move(scores_status.value());
  EXPECT_EQ(3, scores.size());
  EXPECT_EQ(scores[0].first, 0.6);
  EXPECT_EQ(scores[0].second, "c");
  EXPECT_EQ(scores[1].first, 0.3);
  EXPECT_EQ(scores[1].second, "b");
  EXPECT_EQ(scores[2].first, 0.0);
  EXPECT_EQ(scores[2].second, "a");

  scores_status = GetTopHypotheses(scores_proto, /* top_n= */1);
  EXPECT_TRUE(scores_status.ok());
  scores = std::move(scores_status.value());
  EXPECT_EQ(1, scores.size());
  EXPECT_EQ(scores[0].first, 0.6);
  EXPECT_EQ(scores[0].second, "c");
}

TEST(LanguageModelTest, CheckSoftmaxRenormalize) {
  std::vector<double> costs = {
    -std::log(0.4), -std::log(0.1), -std::log(0.3), 0.0 };
  SoftmaxRenormalize(&costs);
  EXPECT_LT(0.0, costs[3]);
  std::vector<double> probs;
  probs.reserve(costs.size());
  std::transform(costs.begin(), costs.end(), std::back_inserter(probs),
                 [](double cost) { return std::exp(-cost); });
  EXPECT_NEAR(1.0, std::accumulate(probs.begin(), probs.end(), 0.0), 1E-6);
}

}  // namespace
}  // namespace models
}  // namespace mozolm
