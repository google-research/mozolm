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

  // Get new estimates.  Adding a two 'a' and one 'b' count at the
  // start state, makes the unigram counts 2 each for a and b and 1 for </S>.
  // Using the PPM formula:
  // P(a | <S>) = (2 - 0.75 + (0.5 + 2 * 0.75) * 0.4) / 3.5 = 0.585714;
  // P(b | <S>) = (1 - 0.75 + (0.5 + 2 * 0.75) * 0.4) / 3.5 = 0.3;
  // and P(</S> | <S>) = (0.5 + 2 * 0.75) * 0.2 / 3.5 =  0.114285.
  LMScores scores;
  ASSERT_TRUE(hub_->ExtractLMScores(start_state_, &scores));
  ASSERT_EQ(3, scores.probabilities_size());
  EXPECT_NEAR(scores.probabilities(0), 0.114285, kEpsilon);  // </S>
  EXPECT_NEAR(scores.probabilities(1), 0.585714, kEpsilon);  // "a"
  EXPECT_NEAR(scores.probabilities(2), 0.3, kEpsilon);  // "b"
}

TEST_F(VocabOnlySingleModelTest, UpdateByNonSingletonCount) {
  CheckUniform();

  // Update the model with single-symbol observations.
  EXPECT_TRUE(hub_->UpdateLMCounts(start_state_, {kAsciiA}, 2));
  EXPECT_TRUE(hub_->UpdateLMCounts(start_state_, {kAsciiB}, 1));

  // Get new estimates.  See formulas above.
  LMScores scores;
  ASSERT_TRUE(hub_->ExtractLMScores(start_state_, &scores));
  ASSERT_EQ(3, scores.probabilities_size());
  EXPECT_NEAR(scores.probabilities(0), 0.114285, kEpsilon);  // </S>
  EXPECT_NEAR(scores.probabilities(1), 0.585714, kEpsilon);  // "a"
  EXPECT_NEAR(scores.probabilities(2), 0.3, kEpsilon);       // "b"
}

TEST_F(VocabOnlySingleModelTest, CheckNextStateAndStateSymbol) {
  constexpr int kBadState = 100;
  int kBadSymbol = hub_->StateSym(kBadState);
  EXPECT_GT(0, kBadSymbol);
  EXPECT_GT(0, hub_->NextState(kBadState, kBadSymbol));

  // By convention, symbol 0 serves as both </S> and <S> symbols.
  const int start_sym = hub_->StateSym(start_state_);
  EXPECT_EQ(0, start_sym);
  EXPECT_EQ(1, hub_->NextState(start_state_, start_sym));
  EXPECT_EQ(start_sym,
            hub_->StateSym(hub_->NextState(start_state_, start_sym)));
  EXPECT_EQ(2, hub_->NextState(start_state_, kAsciiA));
  EXPECT_EQ(kAsciiA, hub_->StateSym(hub_->NextState(start_state_, kAsciiA)));
  EXPECT_EQ(3, hub_->NextState(start_state_, kAsciiB));
  EXPECT_EQ(kAsciiB, hub_->StateSym(hub_->NextState(start_state_, kAsciiB)));
  EXPECT_EQ(4, hub_->NextState(1, start_sym));
  EXPECT_EQ(start_sym, hub_->StateSym(hub_->NextState(1, start_sym)));
  EXPECT_EQ(5, hub_->NextState(1, kAsciiA));
  EXPECT_EQ(kAsciiA, hub_->StateSym(hub_->NextState(1, kAsciiA)));
  EXPECT_EQ(6, hub_->NextState(1, kAsciiB));
  EXPECT_EQ(kAsciiB, hub_->StateSym(hub_->NextState(1, kAsciiB)));
}

TEST_F(VocabOnlySingleModelTest, NonUniformProbsForSequenceWithNextState) {
  CheckUniform();

  // Feed the input until we've reached the state corresponding to last symbol.
  const std::vector<int> symbols = { kAsciiA, kAsciiA, kAsciiA, kAsciiB };
  int curr_state = start_state_;
  for (auto sym : symbols) {
    curr_state = hub_->NextState(curr_state, sym);
    EXPECT_GE(curr_state, 0);
  }
  EXPECT_TRUE(hub_->UpdateLMCounts(curr_state, symbols, 1));

  // Get new estimates.  Because the model was 'empty' when first traversing new
  // states, the final destination of curr_state is the unigram state. The
  // unigram counts update twice for 'a' and once for 'b', so the unigram counts
  // are 3, 2, 1 for a, b, </S> respectively. Thus, relative frequency estimate
  // gives probabilities of 1/2, 1/3 and 1/6 respectively.
  LMScores scores;
  ASSERT_TRUE(hub_->ExtractLMScores(curr_state, &scores));
  ASSERT_EQ(3, scores.probabilities_size());
  EXPECT_NEAR(scores.probabilities(0), 1.0 / 6.0, kEpsilon);  // </S>
  EXPECT_NEAR(scores.probabilities(1), 1.0 / 2.0, kEpsilon);  // "a"
  EXPECT_NEAR(scores.probabilities(2), 1.0 / 3.0, kEpsilon);  // "b"
}

}  // namespace
}  // namespace models
}  // namespace mozolm
