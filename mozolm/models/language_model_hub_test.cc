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

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <utility>

#include "gmock/gmock.h"
#include "nisaba/port/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "mozolm/models/lm_scores.pb.h"
#include "mozolm/models/model_config.pb.h"
#include "mozolm/models/model_factory.h"
#include "mozolm/models/model_storage.pb.h"
#include "mozolm/models/ppm_as_fst_options.pb.h"
#include "nisaba/port/file_util.h"

using ::nisaba::file::WriteTempTextFile;
using ::testing::DoubleEq;
using ::testing::Each;

namespace mozolm {
namespace models {
namespace {

// Name of a temporary vocabulary file.
constexpr char kVocabFileName[] = "vocab.txt";

// Epsilon for floating point comparisons.
constexpr double kEpsilon = 1E-3;

// ASCII for `a` and `b`.
constexpr int kAsciiA = 97;
constexpr int kAsciiB = 98;

class VocabOnlySingleModelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create configuration for the PPM model.
    ModelStorage storage;
    const int max_order = 2;
    storage.mutable_ppm_options()->set_max_order(max_order);
    storage.mutable_ppm_options()->set_static_model(false);
    const auto write_status = WriteTempTextFile(kVocabFileName, "ab");
    ASSERT_OK(write_status.status());
    const std::string vocab_path = write_status.value();
    EXPECT_FALSE(vocab_path.empty());
    storage.set_vocabulary_file(vocab_path);

    // Create model hub config.
    ModelConfig *model_config = config_.add_model_config();
    model_config->set_type(ModelConfig::PPM_AS_FST);
    *model_config->mutable_storage() = storage;

    // Initialize the hub.
    auto hub_status = MakeModelHub(config_);
    ASSERT_TRUE(hub_status.ok()) << "Failed to initialize model hub";
    hub_ = std::move(hub_status.value());
    EXPECT_TRUE(std::filesystem::remove(vocab_path));

    // Fetch start state.
    start_state_ = hub_->ContextState("");
    EXPECT_LE(0, start_state_);
  }

  // Checks that the initial estimates are distributed according to a uniform
  // distribution over the three symbols.
  void CheckUniform() const {
    LMScores scores;
    ASSERT_TRUE(hub_->ExtractLMScores(start_state_, &scores));
    ASSERT_EQ(3, scores.symbols_size());
    EXPECT_EQ("", scores.symbols(0));  // </S>.
    EXPECT_EQ("a", scores.symbols(1));
    EXPECT_EQ("b", scores.symbols(2));
    ASSERT_EQ(3, scores.probabilities_size());
    EXPECT_THAT(scores.probabilities(), Each(DoubleEq(1.0 / 3)));
  }

  ModelHubConfig config_;
  std::unique_ptr<LanguageModelHub> hub_;
  int start_state_;
};

TEST_F(VocabOnlySingleModelTest, NonUniformProbs) {
  CheckUniform();

  // Update the model with single-symbol observations.
  EXPECT_TRUE(hub_->UpdateLMCounts(start_state_, {kAsciiA}, 1));
  EXPECT_TRUE(hub_->UpdateLMCounts(start_state_, {kAsciiA}, 1));
  EXPECT_TRUE(hub_->UpdateLMCounts(start_state_, {kAsciiB}, 1));

  // Get new estimates.
  LMScores scores;
  ASSERT_TRUE(hub_->ExtractLMScores(start_state_, &scores));
  ASSERT_EQ(3, scores.probabilities_size());
  EXPECT_NEAR(scores.probabilities(0), 0.142857, kEpsilon);  // </S>
  EXPECT_NEAR(scores.probabilities(1), 0.571429, kEpsilon);  // "a"
  EXPECT_NEAR(scores.probabilities(2), 0.285714, kEpsilon);  // "b"
}

TEST_F(VocabOnlySingleModelTest, UpdateByNonSingletonCountBug) {
  // TODO: Following is a bug. Can't update by count > 1, have
  // to repeat the call `count` times instead.
  EXPECT_FALSE(hub_->UpdateLMCounts(start_state_, {kAsciiA}, 2));
}

TEST_F(VocabOnlySingleModelTest, CheckNextStateAndStateSymbol) {
  constexpr int kBadSymbol = 1234;
  EXPECT_GT(0, hub_->StateSym(kBadSymbol));
  // TODO: Following line exposes another bug. The call actually
  // succeeds in adding a state, but it shouldn't.
  // constexpr int kBadState = 100;
  // EXPECT_LT(0, hub_->NextState(kBadState, kBadSymbol));

  const int start_sym = hub_->StateSym(start_state_);
  EXPECT_EQ(0, start_sym);  // "</S>"
  EXPECT_EQ(1, hub_->NextState(start_state_, /* </S> */0));
  EXPECT_EQ(2, hub_->NextState(start_state_, kAsciiA));
  EXPECT_EQ(3, hub_->NextState(start_state_, kAsciiB));
  EXPECT_EQ(4, hub_->NextState(1, /* </S> */0));
  EXPECT_EQ(5, hub_->NextState(1, kAsciiA));
  EXPECT_EQ(6, hub_->NextState(1, kAsciiB));
}

TEST_F(VocabOnlySingleModelTest, NonUniformProbsForSequenceWithNextState) {
  CheckUniform();

  // Feed the input until we've reached the state corresponding to last symbol.
  const std::vector<int> symbols = { kAsciiA, kAsciiA, kAsciiA, kAsciiB };
  int curr_state = start_state_;
  for (auto sym : symbols) {
    curr_state = hub_->NextState(curr_state, sym);
    EXPECT_GE(start_state_, 0);
  }
  EXPECT_TRUE(hub_->UpdateLMCounts(start_state_, symbols, 1));

  // Fetch the scores from the last state.
  LMScores scores;
  ASSERT_TRUE(hub_->ExtractLMScores(curr_state, &scores));
  ASSERT_EQ(3, scores.probabilities_size());
  EXPECT_NEAR(scores.probabilities(0), 0.222222, kEpsilon);  // </S>
  EXPECT_NEAR(scores.probabilities(1), 0.444444, kEpsilon);  // "a"
  EXPECT_NEAR(scores.probabilities(2), 0.333333, kEpsilon);  // "b"
}

}  // namespace
}  // namespace models
}  // namespace mozolm
