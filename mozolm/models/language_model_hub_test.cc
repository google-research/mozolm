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
const char kVocabFileName[] = "vocab.txt";

// Epsilon for floating point comparisons.
const double kEpsilon = 1E-3;

TEST(LanguageModelHubTest, VocabOnlyPpmModel) {
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
  ModelHubConfig config;
  ModelConfig *model_config = config.add_model_config();
  model_config->set_type(ModelConfig::PPM_AS_FST);
  *model_config->mutable_storage() = storage;

  // Initialize the hub.
  auto hub_status = MakeModelHub(config);
  ASSERT_TRUE(hub_status.ok()) << "Failed to initialize model hub";
  std::unique_ptr<LanguageModelHub> hub = std::move(hub_status.value());
  EXPECT_TRUE(std::filesystem::remove(vocab_path));

  // Retrieve initial estimates: These should correspond to the uniform
  // distribution over the three symbols.
  LMScores scores;
  const int start_state = hub->ContextState("");
  ASSERT_TRUE(hub->ExtractLMScores(start_state, &scores));
  ASSERT_EQ(3, scores.symbols_size());
  EXPECT_EQ("", scores.symbols(0));  // </S>.
  EXPECT_EQ("a", scores.symbols(1));
  EXPECT_EQ("b", scores.symbols(2));
  ASSERT_EQ(3, scores.probabilities_size());
  EXPECT_THAT(scores.probabilities(), Each(DoubleEq(1.0 / 3)));

  // Update the model.
  // TODO: Following is a bug. Can't update by count > 1, have
  // to repeat the call `count` times instead.
  EXPECT_TRUE(hub->UpdateLMCounts(
      start_state, {97}, 1));  // Updates "a" count.
  EXPECT_TRUE(hub->UpdateLMCounts(
      start_state, {97}, 1));  // Updates "a" count.
  EXPECT_TRUE(hub->UpdateLMCounts(
      start_state, {98}, 1));  // Updates "b" count.

  // Get new estimates.
  ASSERT_TRUE(hub->ExtractLMScores(start_state, &scores));
  ASSERT_EQ(3, scores.probabilities_size());
  EXPECT_NEAR(scores.probabilities(0), 0.142857, kEpsilon);  // </S>
  EXPECT_NEAR(scores.probabilities(1), 0.571429, kEpsilon);  // "a"
  EXPECT_NEAR(scores.probabilities(2), 0.285714, kEpsilon);  // "b"
}

}  // namespace
}  // namespace models
}  // namespace mozolm
