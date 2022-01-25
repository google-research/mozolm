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

// Unit tests for a simple bigram character model.

#include "mozolm/models/simple_bigram_char_model.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "nisaba/port/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_join.h"
#include "mozolm/models/model_storage.pb.h"
#include "mozolm/models/model_test_utils.h"
#include "nisaba/port/utf8_util.h"
#include "nisaba/port/test_utils.h"

using nisaba::testing::TestFilePath;

namespace mozolm {
namespace models {
namespace {

// Simple bigram model trained on Wikipedia stored as a dense matrix.
constexpr char kModelDir[] =
    "com_google_mozolm/mozolm/models/testdata";
constexpr char kMatrixName[] = "en_wiki_1Mline_char_bigram.matrix.txt";
constexpr char kRowsName[] = "en_wiki_1Mline_char_bigram.rows.txt";

constexpr char kSampleText[] = R"(
    As a Senator he backed the amendment to the Colombian Constitution
    permitting Presidential re-election.)";

// The tests run on a simple bigram character model stored in a dense matrix.
class SimpleBigramCharModelTest : public ::testing::Test {
 protected:
  void Init(const std::string &model_dir, const std::string &model_name,
            const std::string &vocab_name) {
    model_storage_.set_vocabulary_file(TestFilePath(model_dir, vocab_name));
    model_storage_.set_model_file(TestFilePath(model_dir, model_name));
    const auto read_status = model_.Read(model_storage_);
    ASSERT_TRUE(read_status.ok()) << "Failed to read model: "
                                  << read_status.ToString();
  }

  void Init() {
    Init(kModelDir, kMatrixName, kRowsName);
  }

  void CheckTopCandidateForContext(const std::string &context,
                                   std::pair<double, std::string> *cand) {
    return ::mozolm::models::CheckTopCandidateForContext(
        context, &model_, cand);
  }

  ModelStorage model_storage_;
  SimpleBigramCharModel model_;
};

// If no model file given, initializes uniform over lower-case ASCII letters,
// space and end-of-string.
TEST_F(SimpleBigramCharModelTest, CheckEmpty) {
  Init();
  SimpleBigramCharModel model;
  ModelStorage empty_storage;
  EXPECT_TRUE(model.Read(empty_storage).ok());
  EXPECT_EQ(model.NumSymbols(), 28);
}

// Fails if given bad vocabulary name.
TEST_F(SimpleBigramCharModelTest, CheckNonExistent) {
  Init();
  SimpleBigramCharModel model;
  ModelStorage bad_storage;
  bad_storage.set_vocabulary_file("bar");
  EXPECT_FALSE(model.Read(bad_storage).ok());
}

// Ignores model file if no vocabulary given, initializes uniform over
// lower-case ASCII letters, space and end-of-string.
TEST_F(SimpleBigramCharModelTest, CheckSkipsModelFile) {
  Init();
  SimpleBigramCharModel model;
  ModelStorage bad_storage;
  bad_storage.set_model_file("foo");
  EXPECT_TRUE(model.Read(bad_storage).ok());
  EXPECT_EQ(model.NumSymbols(), 28);
}

TEST_F(SimpleBigramCharModelTest, BasicCheck) {
  Init();

  const std::vector<int> input_chars = nisaba::utf8::StrSplitByCharToUnicode(
      kSampleText);
  int state = -1;
  for (int input_char : input_chars) {
    state = model_.NextState(state, input_char);
    LMScores result;
    EXPECT_TRUE(model_.ExtractLMScores(state, &result));
    EXPECT_EQ(model_.NumSymbols(), result.symbols().size());
    EXPECT_EQ(model_.NumSymbols(), result.probabilities().size());

    const auto &probs = result.probabilities();
    const double total_prob = std::accumulate(probs.begin(), probs.end(), 0.0);
    EXPECT_NEAR(1.0, total_prob, 1E-6);
    const auto &syms = result.symbols();
    for (int i = 0; i < syms.size(); ++i) {
      int utf8_sym;
      if (syms[i].empty()) {
        utf8_sym = 0;
      } else {
        EXPECT_TRUE(nisaba::utf8::DecodeSingleUnicodeChar(syms[i], &utf8_sym));
      }
      EXPECT_NEAR(model_.SymLMScore(state, utf8_sym), -std::log(probs[i]),
                  1E-6);
    }
  }
}

TEST_F(SimpleBigramCharModelTest, TopCandidates) {
  Init();
  constexpr int kMaxString = 15;
  std::string buffer = "H";
  int state = -1;
  for (int i = 0; i < kMaxString; ++i) {
    state = model_.ContextState(buffer);
    LMScores result;
    EXPECT_TRUE(model_.ExtractLMScores(state, &result));
    const auto scores_status = GetTopHypotheses(result, /* top_n= */1);
    EXPECT_TRUE(scores_status.ok());
    const auto scores = std::move(scores_status.value());
    buffer += scores[0].second;
  }
  EXPECT_EQ("He the the the t", buffer);
}

}  // namespace
}  // namespace models
}  // namespace mozolm
