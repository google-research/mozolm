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

// Names of a temporary vocabulary files.
constexpr char kVocabFileName[] = "vocab.txt";
constexpr char kSmallVocabFileName[] = "vocab_small.txt";

// Epsilon for floating point comparisons.
constexpr double kEpsilon = 1E-3;

// ASCII for `a` and `b`.
constexpr int kAsciiA = 97;
constexpr int kAsciiB = 98;

class VocabOnlyModelsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::vector<ModelStorage> models;
    // Create configuration for first PPM model, vocabulary a, b.
    auto write_status = WriteTempTextFile(kVocabFileName, "ab");
    ASSERT_OK(write_status.status());
    EXPECT_FALSE(write_status.value().empty());
    models.push_back(CreateModelStorageConfig(write_status.value()));

    // Create model hub config for single model hub.
    CreateModelHub(models, /*bayesian_history_length=*/0);

    // Create configuration for second PPM model, vocabulary just a (no b).
    write_status = WriteTempTextFile(kSmallVocabFileName, "a");
    ASSERT_OK(write_status.status());
    EXPECT_FALSE(write_status.value().empty());
    models.push_back(CreateModelStorageConfig(write_status.value()));

    // Create model hub config for uniform mixture two model hub.
    CreateModelHub(models, /*bayesian_history_length=*/0);

    // Create model hub config for bayes mixture two model hub.
    CreateModelHub(models, /*bayesian_history_length=*/2);

    EXPECT_TRUE(std::filesystem::remove(models[0].vocabulary_file()));
    EXPECT_TRUE(std::filesystem::remove(models[1].vocabulary_file()));
  }

  // Creates model storage configuration from vocabulary file.
  ModelStorage CreateModelStorageConfig(const std::string &vocab_path) {
    ModelStorage storage;
    const int max_order = 2;
    storage.mutable_ppm_options()->set_max_order(max_order);
    storage.mutable_ppm_options()->set_static_model(false);
    storage.set_vocabulary_file(vocab_path);
    return storage;
  }

  // Creates model hubs given the vector of models and bayesian history length
  // parameter.
  void CreateModelHub(const std::vector<ModelStorage> models,
                      int bayesian_history_length) {
    ModelHubConfig hub_config;
    hub_config.set_mixture_type(ModelHubConfig::INTERPOLATION);
    hub_config.set_bayesian_history_length(bayesian_history_length);
    ModelConfig *model_config = hub_config.add_model_config();
    model_config->set_type(ModelConfig::PPM_AS_FST);
    *model_config->mutable_storage() = models[0];
    if (models.size() > 1) {
      ModelConfig *second_model_config = hub_config.add_model_config();
      second_model_config->set_type(ModelConfig::PPM_AS_FST);
      *second_model_config->mutable_storage() = models[1];
    }
    auto hub_status = MakeModelHub(hub_config);
    ASSERT_TRUE(hub_status.ok()) << "Failed to initialize model hub";
    if (models.size() == 1) {
      // Initializes the single model hub and sets start state.
      one_model_hub_ = std::move(hub_status.value());
      one_model_start_state_ = one_model_hub_->ContextState("");
      EXPECT_LE(0, one_model_start_state_);
    } else {
      if (bayesian_history_length > 0) {
        // Initializes the bayes two model hub and sets start state.
        bayes_two_model_hub_ = std::move(hub_status.value());
        bayes_two_model_start_state_ = bayes_two_model_hub_->ContextState("");
        EXPECT_LE(0, bayes_two_model_start_state_);
      } else {
        // Initializes the uniform two model hub and sets start state.
        uniform_two_model_hub_ = std::move(hub_status.value());
        uniform_two_model_start_state_ =
            uniform_two_model_hub_->ContextState("");
        EXPECT_LE(0, uniform_two_model_start_state_);
      }
    }
  }

  // Checks that the initial estimates of the single model are distributed
  // according to a uniform distribution over the three symbols.
  void CheckUniform() const {
    LMScores scores;
    ASSERT_TRUE(
        one_model_hub_->ExtractLMScores(one_model_start_state_, &scores));
    ASSERT_EQ(3, scores.symbols_size());
    EXPECT_EQ("", scores.symbols(0));  // </S>.
    EXPECT_EQ("a", scores.symbols(1));
    EXPECT_EQ("b", scores.symbols(2));
    ASSERT_EQ(3, scores.probabilities_size());
    EXPECT_THAT(scores.probabilities(), Each(DoubleEq(1.0 / 3)));
  }

  // Checks that the initial estimates of the two model hubs are a uniform
  // mixture of uniform distributions.  Since there have been no updates to the
  // Bayesian mixture model, the results should be identical with the uniform
  // mixture.  The model with 3 outcomes (a, b and </S>) has 1/3 probability for
  // each; the model with 2 outcomes (a and </S>) has 1/2 probablity each.  So
  // the mixture should provide 1/6 + 1/4 = 5/12 for a and </S> and 2/12 for b.
  void CheckInitialTwoModelMix() const {
    LMScores scores;
    ASSERT_TRUE(uniform_two_model_hub_->ExtractLMScores(
        uniform_two_model_start_state_, &scores));
    ASSERT_EQ(3, scores.symbols_size());
    EXPECT_EQ("", scores.symbols(0));  // </S>.
    EXPECT_EQ("a", scores.symbols(1));
    EXPECT_EQ("b", scores.symbols(2));
    ASSERT_EQ(3, scores.probabilities_size());
    EXPECT_THAT(scores.probabilities(0), DoubleEq(5.0 / 12));
    EXPECT_THAT(scores.probabilities(1), DoubleEq(5.0 / 12));
    EXPECT_THAT(scores.probabilities(2), DoubleEq(2.0 / 12));
    LMScores scores_bayes;  // Bayes model should yield the same.
    ASSERT_TRUE(bayes_two_model_hub_->ExtractLMScores(
        uniform_two_model_start_state_, &scores_bayes));
    ASSERT_EQ(scores_bayes.symbols_size(), scores.symbols_size());
    EXPECT_EQ(scores_bayes.symbols(0), scores.symbols(0));
    EXPECT_EQ(scores_bayes.symbols(1), scores.symbols(1));
    EXPECT_EQ(scores_bayes.symbols(2), scores.symbols(2));
    ASSERT_EQ(scores_bayes.probabilities_size(), scores.probabilities_size());
    EXPECT_THAT(scores_bayes.probabilities(0),
                DoubleEq(scores.probabilities(0)));
    EXPECT_THAT(scores_bayes.probabilities(1),
                DoubleEq(scores.probabilities(1)));
    EXPECT_THAT(scores_bayes.probabilities(2),
                DoubleEq(scores.probabilities(2)));
  }

  std::unique_ptr<LanguageModelHub> one_model_hub_;
  int one_model_start_state_;
  std::unique_ptr<LanguageModelHub> uniform_two_model_hub_;
  int uniform_two_model_start_state_;
  std::unique_ptr<LanguageModelHub> bayes_two_model_hub_;
  int bayes_two_model_start_state_;
};

TEST_F(VocabOnlyModelsTest, NonUniformProbs) {
  CheckUniform();

  // Update the model with single-symbol observations.
  EXPECT_TRUE(
      one_model_hub_->UpdateLMCounts(one_model_start_state_, {kAsciiA}, 1));
  EXPECT_TRUE(
      one_model_hub_->UpdateLMCounts(one_model_start_state_, {kAsciiA}, 1));
  EXPECT_TRUE(
      one_model_hub_->UpdateLMCounts(one_model_start_state_, {kAsciiB}, 1));

  // Get new estimates.  Adding a two 'a' and one 'b' count at the
  // start state, makes the unigram counts 2 each for a and b and 1 for </S>.
  // Using the PPM formula:
  // P(a | <S>) = (2 - 0.75 + (0.5 + 2 * 0.75) * 0.4) / 3.5 = 0.585714;
  // P(b | <S>) = (1 - 0.75 + (0.5 + 2 * 0.75) * 0.4) / 3.5 = 0.3;
  // and P(</S> | <S>) = (0.5 + 2 * 0.75) * 0.2 / 3.5 =  0.114285.
  LMScores scores;
  ASSERT_TRUE(one_model_hub_->ExtractLMScores(one_model_start_state_, &scores));
  ASSERT_EQ(3, scores.probabilities_size());
  EXPECT_NEAR(scores.probabilities(0), 0.114285, kEpsilon);  // </S>
  EXPECT_NEAR(scores.probabilities(1), 0.585714, kEpsilon);  // "a"
  EXPECT_NEAR(scores.probabilities(2), 0.3, kEpsilon);  // "b"
}

TEST_F(VocabOnlyModelsTest, UpdateByNonSingletonCount) {
  CheckUniform();

  // Update the model with single-symbol observations.
  EXPECT_TRUE(
      one_model_hub_->UpdateLMCounts(one_model_start_state_, {kAsciiA}, 2));
  EXPECT_TRUE(
      one_model_hub_->UpdateLMCounts(one_model_start_state_, {kAsciiB}, 1));

  // Get new estimates.  See formulas above.
  LMScores scores;
  ASSERT_TRUE(one_model_hub_->ExtractLMScores(one_model_start_state_, &scores));
  ASSERT_EQ(3, scores.probabilities_size());
  EXPECT_NEAR(scores.probabilities(0), 0.114285, kEpsilon);  // </S>
  EXPECT_NEAR(scores.probabilities(1), 0.585714, kEpsilon);  // "a"
  EXPECT_NEAR(scores.probabilities(2), 0.3, kEpsilon);       // "b"
}

TEST_F(VocabOnlyModelsTest, CheckNextStateAndStateSymbol) {
  constexpr int kBadState = 100;
  int kBadSymbol = one_model_hub_->StateSym(kBadState);
  EXPECT_GT(0, kBadSymbol);
  EXPECT_GT(0, one_model_hub_->NextState(kBadState, kBadSymbol));

  // By convention, symbol 0 serves as both </S> and <S> symbols.
  const int start_sym = one_model_hub_->StateSym(one_model_start_state_);
  EXPECT_EQ(0, start_sym);
  EXPECT_EQ(1, one_model_hub_->NextState(one_model_start_state_, start_sym));
  EXPECT_EQ(start_sym, one_model_hub_->StateSym(one_model_hub_->NextState(
                           one_model_start_state_, start_sym)));
  EXPECT_EQ(2, one_model_hub_->NextState(one_model_start_state_, kAsciiA));
  EXPECT_EQ(kAsciiA, one_model_hub_->StateSym(one_model_hub_->NextState(
                         one_model_start_state_, kAsciiA)));
  EXPECT_EQ(3, one_model_hub_->NextState(one_model_start_state_, kAsciiB));
  EXPECT_EQ(kAsciiB, one_model_hub_->StateSym(one_model_hub_->NextState(
                         one_model_start_state_, kAsciiB)));
  EXPECT_EQ(4, one_model_hub_->NextState(1, start_sym));
  EXPECT_EQ(start_sym,
            one_model_hub_->StateSym(one_model_hub_->NextState(1, start_sym)));
  EXPECT_EQ(5, one_model_hub_->NextState(1, kAsciiA));
  EXPECT_EQ(kAsciiA,
            one_model_hub_->StateSym(one_model_hub_->NextState(1, kAsciiA)));
  EXPECT_EQ(6, one_model_hub_->NextState(1, kAsciiB));
  EXPECT_EQ(kAsciiB,
            one_model_hub_->StateSym(one_model_hub_->NextState(1, kAsciiB)));
}

TEST_F(VocabOnlyModelsTest, NonUniformProbsForSequenceWithNextState) {
  CheckUniform();

  // Feed the input until we've reached the state corresponding to last symbol.
  const std::vector<int> symbols = {kAsciiA, kAsciiA, kAsciiA, kAsciiB};
  int curr_state = one_model_start_state_;
  for (auto sym : symbols) {
    curr_state = one_model_hub_->NextState(curr_state, sym);
    EXPECT_GE(curr_state, 0);
  }
  EXPECT_TRUE(one_model_hub_->UpdateLMCounts(curr_state, symbols, 1));

  // Get new estimates.  Because the model was 'empty' when first traversing new
  // states, the final destination of curr_state is the unigram state. The
  // unigram counts update twice for 'a' and once for 'b', so the unigram counts
  // are 3, 2, 1 for a, b, </S> respectively. Thus, relative frequency estimate
  // gives probabilities of 1/2, 1/3 and 1/6 respectively.
  LMScores scores;
  ASSERT_TRUE(one_model_hub_->ExtractLMScores(curr_state, &scores));
  ASSERT_EQ(3, scores.probabilities_size());
  EXPECT_NEAR(scores.probabilities(0), 1.0 / 6.0, kEpsilon);  // </S>
  EXPECT_NEAR(scores.probabilities(1), 1.0 / 2.0, kEpsilon);  // "a"
  EXPECT_NEAR(scores.probabilities(2), 1.0 / 3.0, kEpsilon);  // "b"
}

TEST_F(VocabOnlyModelsTest, UniformMixtureTest) {
  CheckInitialTwoModelMix();
  EXPECT_TRUE(uniform_two_model_hub_->UpdateLMCounts(
      uniform_two_model_start_state_, {kAsciiA}, 1));
  const int next_state = uniform_two_model_hub_->NextState(
      uniform_two_model_start_state_, kAsciiA);

  // Fetch the scores from the updated start state.
  // In the PPM model, the next state has no observations, so makes use of the
  // uniform distribution in both models being mixed.  Let P(x) be the
  // probability of x in the model with the small vocabulary (2 outcomes) and
  // Q(x) the probability of x in the model with larger vocabulary (3 outcomes).
  // Since 'a' was updated, with Laplace smoothing, P(a) = 2/3 and P(</S>) =
  // 1/3, while Q(a) = 1/2 and Q(b) = Q(</S> = 1/4.  At a 50/50 mixture, the
  // overall probability of a is 2/6 + 1/4 = 7/12; the overall probability of b
  // is 1/8; and the overall probability of </S> is 1/6 + 1/8 = 7/24.
  LMScores scores;
  ASSERT_TRUE(uniform_two_model_hub_->ExtractLMScores(next_state, &scores));
  ASSERT_EQ(3, scores.probabilities_size());
  EXPECT_NEAR(scores.probabilities(0), 7.0 / 24.0, kEpsilon);  // </S>
  EXPECT_NEAR(scores.probabilities(1), 7.0 / 12.0, kEpsilon);  // "a"
  EXPECT_NEAR(scores.probabilities(2), 1.0 / 8.0, kEpsilon);   // "b"
}

TEST_F(VocabOnlyModelsTest, BayesMixtureTest) {
  CheckInitialTwoModelMix();
  EXPECT_TRUE(bayes_two_model_hub_->UpdateLMCounts(
      bayes_two_model_start_state_, {kAsciiA}, 1));
  const int next_state =
      bayes_two_model_hub_->NextState(bayes_two_model_start_state_, kAsciiA);

  // Fetch the scores from the updated next state.
  // Let P(x) be the probability of x in the model with the small vocabulary (2
  // outcomes) and Q(x) the probability of x in the model with larger vocabulary
  // (3 outcomes).  For Bayesian mixing, the updated mixture at next_state is
  // based on the probability of <S>a in the model BEFORE the update, while the
  // actual model probabilities being mixed are taken from AFTER the update.
  // Since 'a' was updated, with Laplace smoothing, P(a) = 2/3 and P(</S>) =
  // 1/3, while Q(a) = 1/2 and Q(b) = Q(</S> = 1/4.  For Bayesian mixing, the
  // updated mixture at next_state is based on the probability of <S>a in the
  // model before the update, which is 1/2 for P and 1/3 for Q, which becomes
  // 3/5 and 2/5 after normalization. Thus the overall probability for a is:
  // 2/5 * 1/2 + 3/5 * 2/3 = 3/5; total for b is 2/5 * 1/4 = 1/10; and for </S>
  // 2/5 * 1/4 + 3/5 * 1/3 = 3/10.
  LMScores scores;
  ASSERT_TRUE(bayes_two_model_hub_->ExtractLMScores(next_state, &scores));
  ASSERT_EQ(3, scores.probabilities_size());
  EXPECT_NEAR(scores.probabilities(0), 0.3, kEpsilon);  // </S>
  EXPECT_NEAR(scores.probabilities(1), 0.6, kEpsilon);  // "a"
  EXPECT_NEAR(scores.probabilities(2), 0.1, kEpsilon);  // "b"
}

}  // namespace
}  // namespace models
}  // namespace mozolm
