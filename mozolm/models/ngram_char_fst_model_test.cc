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

// Unit tests for a simple n-gram character model.

#include "mozolm/models/ngram_char_fst_model.h"

#include <algorithm>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

#include "fst/vector-fst.h"
#include "gmock/gmock.h"
#include "mozolm/stubs/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_join.h"
#include "mozolm/models/model_storage.pb.h"
#include "mozolm/utils/utf8_util.h"

using fst::StdArc;

namespace mozolm {
namespace models {
namespace {

// Simple model trained on "Alice and Wonderland" and "Adventures of Sherlock
// Holmes" from Project Gutenberg.
const char kModelDir[] = "mozolm/models/testdata";
const char kModelName[] = "gutenberg_en_char_ngram_o4_wb.fst";

// Third-party model from Michigan Tech (MTU).
const char kThirdPartyModelDir[] =
    "third_party/models/mtu";
const char kThirdPartyModelName[] = "dasher_feb21_eng_char_4gram.fst";

const char kSampleText[] = R"(
    His manner was not effusive. It seldom was; but he was glad, I think,
    to see me. With hardly a word spoken, but with a kindly eye, he waved
    me to an armchair, threw across his case of cigars, and indicated a
    spirit case and a gasogene in the corner. Then he stood before the fire
    and looked me over in his singular introspective fashion.)";

// Helper stub for the n-gram model.
class NGramCharFstModelHelper : public NGramCharFstModel {
 public:
  using NGramCharFstModel::LabelCostInState;
};

// The tests run on a simple 4-gram character model using standard Witten-Bell
// smoothing trained on a portion of English books from the Gutenberg corpus.
class NGramCharFstModelTest : public ::testing::Test {
 protected:
  void Init(const std::string &model_dir, const std::string &model_name) {
    const std::filesystem::path model_path = (
        std::filesystem::current_path() /
        model_dir / model_name).make_preferred();
    model_storage_.set_model_file(model_path.string());
    const auto read_status = model_.Read(model_storage_);
    ASSERT_TRUE(read_status.ok()) << "Failed to read model: "
                                  << read_status.ToString();
  }

  void Init() {
    Init(kModelDir, kModelName);
  }

  void TopCandidateForContext(const std::string &context,
                              std::pair<double, std::string> *cand) {
    const int state = model_.ContextState(context);
    EXPECT_LT(0, state);
    LMScores result;
    EXPECT_TRUE(model_.ExtractLMScores(state, &result));
    EXPECT_LT(0, result.normalization());
    const auto scores_status = GetTopHypotheses(result, /* top_n= */1);
    EXPECT_TRUE(scores_status.ok());
    const auto scores = std::move(scores_status.value());
    *cand = std::move(scores[0]);
    EXPECT_LT(0.0, cand->first);
  }

  ModelStorage model_storage_;
  NGramCharFstModelHelper model_;
};

TEST_F(NGramCharFstModelTest, CheckNonExistent) {
  Init();
  NGramCharFstModel model;
  ModelStorage bad_storage;
  EXPECT_FALSE(model.Read(bad_storage).ok());
  bad_storage.set_model_file("foo");
  EXPECT_FALSE(model.Read(bad_storage).ok());
}

TEST_F(NGramCharFstModelTest, BasicCheck) {
  Init();
  const auto &fst = model_.fst();
  const int num_symbols = fst.InputSymbols()->NumSymbols();
  EXPECT_LT(1, num_symbols);  // Epsilon + other letters.

  const std::vector<int> input_chars = utf8::StrSplitByCharToUnicode(
      kSampleText);
  int state = -1;
  for (int input_char : input_chars) {
    state = model_.NextState(state, input_char);
    LMScores result;
    EXPECT_TRUE(model_.ExtractLMScores(state, &result));
    EXPECT_EQ(num_symbols - 1, result.symbols().size());
    EXPECT_EQ(num_symbols - 1, result.probabilities().size());

    const auto &probs = result.probabilities();
    const double total_prob = std::accumulate(probs.begin(), probs.end(), 0.0);
    EXPECT_NEAR(1.0, total_prob, 1E-6);
  }

  // Check OOV symbol.
  constexpr int kOutOfVocabQuery = 9924;  // ⛄
  EXPECT_EQ(StdArc::Weight::Zero(), model_.LabelCostInState(
      model_.fst().Start(), kOutOfVocabQuery));
}

TEST_F(NGramCharFstModelTest, TopCandidates) {
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
  EXPECT_EQ("He was the said ", buffer);
}

TEST_F(NGramCharFstModelTest, CheckInDomain) {
  Init();

  // Check for "Alice" as the highly likely word predicted by the model which
  // was trained on "Alice's Adventures in Wonderland".
  std::pair<double, std::string> top_next;
  TopCandidateForContext("Ali", &top_next);
  EXPECT_EQ("c", top_next.second);
  TopCandidateForContext("Alice", &top_next);
  EXPECT_EQ(" ", top_next.second);

  // Check for Sherlock Holmes.
  TopCandidateForContext("Holm", &top_next);
  EXPECT_EQ("e", top_next.second);
  TopCandidateForContext("Holme", &top_next);
  EXPECT_EQ("s", top_next.second);
}

// Check that we can load the FSTs converted from third-party models.
TEST_F(NGramCharFstModelTest, ThirdPartyBasicTest) {
  Init(kThirdPartyModelDir, kThirdPartyModelName);

  // TODO: Predictions are all belly-up at the moment. Please
  // double-check, fix and extend this test..
}

}  // namespace
}  // namespace models
}  // namespace mozolm
