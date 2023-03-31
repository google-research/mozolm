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

#include "mozolm/models/ngram_word_fst_model.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "fst/arcsort.h"
#include "fst/symbol-table.h"
#include "fst/vector-fst.h"
#include "gmock/gmock.h"
#include "nisaba/port/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "mozolm/models/model_storage.pb.h"
#include "mozolm/models/model_test_utils.h"
#include "nisaba/port/file_util.h"
#include "nisaba/port/test_utils.h"
#include "nisaba/port/utf8_util.h"

using ::nisaba::testing::TestFilePath;
using ::fst::ArcSort;
using ::fst::ILabelCompare;
using ::fst::StdArc;
using ::fst::StdVectorFst;
using ::fst::SymbolTable;

namespace mozolm {
namespace models {
namespace {

constexpr float kFloatDelta = 0.00001;  // Delta for float comparisons.

class NGramWordFstTest : public ::testing::Test {
 protected:
  // Creates FST trigram count file, to test FST model initialization.
  void CreateFstWordTrigramModelFile() {
    SymbolTable syms;
    syms.AddSymbol("<epsilon>");
    syms.AddSymbol("aa");
    syms.AddSymbol("ab");
    syms.AddSymbol("ba");
    syms.AddSymbol("bbb");
    StdVectorFst fst;

    // Creates trigram model file in ngram format from 2-line corpus:
    // aa ab ba bbb
    // aa ba ab bbb
    // which results in the following n-grams (and counts), where <S> is
    // begin-of-string and </S> is end-of-string.  For each order of n-grams, we
    // create states corresponding to the n-gram prefixes, i.e., the symbols up
    // to the last one.  For unigrams, there is just a single state.

    // Unigrams:
    //
    // aa       2
    // ab       2
    // ba       2
    // bbb      2
    // </S>     2
    int unigram_state = fst.AddState();

    // Bigrams:
    //
    // <S> aa   2
    // aa ab    1
    // aa ba    1
    // ab ba    1
    // ab bbb   1
    // ba ab    1
    // ba bbb   1
    // bbb </S> 2
    int start_state = fst.AddState();
    fst.SetStart(start_state);
    int aa_state  = fst.AddState();
    int ab_state = fst.AddState();
    int ba_state = fst.AddState();
    int bbb_state = fst.AddState();

    // Trigrams:
    //
    // <S> aa ab        1
    // <S> aa ba        1
    // aa ab ba 1
    // aa ba ab 1
    // ab ba bbb        1
    // ab bbb </S>      1
    // ba ab bbb        1
    // ba bbb </S>      1
    int start_aa_state = fst.AddState();
    int aa_ab_state = fst.AddState();
    int aa_ba_state = fst.AddState();
    int ab_ba_state = fst.AddState();
    int ab_bbb_state = fst.AddState();
    int ba_ab_state = fst.AddState();
    int ba_bbb_state = fst.AddState();

    // We can now add an arc for each of the above n-grams, from the state
    // corresponding to its prefix to the state corresponding to its suffix. For
    // the unigram state, we use no smoothing, and the suffix state is the state
    // associated with the symbol. End-of-string probabilities are assigned as a
    // final cost.  Each option is assigned -logP cost, where here P=1/5.

    fst.AddArc(unigram_state, StdArc(1, 1, -std::log(0.2), aa_state));  // aa 2
    fst.AddArc(unigram_state, StdArc(2, 2, -std::log(0.2), ab_state));  // ab 2
    fst.AddArc(unigram_state, StdArc(3, 3, -std::log(0.2), ba_state));  // ba 2
    fst.AddArc(unigram_state,
               StdArc(4, 4, -std::log(0.2), bbb_state));  // bbb 2
    fst.SetFinal(unigram_state, -std::log(0.2));          // </S>     2

    // For bigram states, we will use simple add-one smoothing, and backoff to
    // the unigram state.  The backoff weight is calculated in closed form to
    // ensure normalization, the details of which we will skip here for brevity.
    // For example, the start state has 2 counts for the bigram "<S> aa", so the
    // probability of that bigram is 2/3, since we add-one to the total from
    // that state.  The remaining 1/3 probability mass is shared evenly among
    // the other 4 options, meaning that each gets 1/12 of the probability mass.
    // The backoff arc value (labeled <epsilon> index 0) ensures that the
    // probabilities of those options are correct.
    fst.AddArc(start_state,
               StdArc(1, 1, -std::log(0.666667), start_aa_state));  // <S> aa 2
    fst.AddArc(start_state, StdArc(0, 0, -std::log(5.0 / 12.0),
                                   unigram_state));  // backoff arc.
    fst.AddArc(aa_state,
               StdArc(2, 2, -std::log(0.333333), aa_ab_state));  // aa ab 1
    fst.AddArc(aa_state,
               StdArc(3, 3, -std::log(0.333333), aa_ba_state));  // aa ba 1
    fst.AddArc(aa_state, StdArc(0, 0, -std::log(5.0 / 9.0),
                                unigram_state));  // backoff arc.
    fst.AddArc(ab_state,
               StdArc(3, 3, -std::log(0.333333), ab_ba_state));  // ab ba 1
    fst.AddArc(ab_state,
               StdArc(4, 4, -std::log(0.333333), ab_bbb_state));  // ab bbb   1
    fst.AddArc(ab_state, StdArc(0, 0, -std::log(5.0 / 9.0),
                                unigram_state));  // backoff arc.
    fst.AddArc(ba_state,
               StdArc(2, 2, -std::log(0.333333), ba_ab_state));  // ba ab 1
    fst.AddArc(ba_state,
               StdArc(4, 4, -std::log(0.333333), ba_bbb_state));  // ba bbb   1
    fst.AddArc(ba_state, StdArc(0, 0, -std::log(5.0 / 9.0),
                                unigram_state));  // backoff arc.
    fst.SetFinal(bbb_state, -std::log(0.666667));  // bbb </S> 2
    fst.AddArc(bbb_state, StdArc(0, 0, -std::log(5.0 / 12.0),
                                 unigram_state));  // backoff arc.

    // For trigram states, we also use add-one smoothing and this time backoff
    // to the state associated with the middle symbol. For example, from the
    // start_aa_state, we have 2 observations of 1 each, so with add-one
    // smoothing we get 1/3 probability for each of those.  The remaining 3
    // possibilities must fit within the remaining 1/3 probability mass and the
    // backoff cost scales appropriately.  In fact, for that state they do, so
    // no scaling is required, i.e., backoff cost is 0.0.
    fst.AddArc(start_aa_state,
               StdArc(2, 2, -std::log(0.333333), aa_ab_state));  // <S> aa ab 1
    fst.AddArc(start_aa_state,
               StdArc(3, 3, -std::log(0.333333), aa_ba_state));  // <S> aa ba 1
    fst.AddArc(start_aa_state, StdArc(0, 0, 0.0, aa_state));     // backoff arc.
    fst.AddArc(aa_ab_state,
               StdArc(3, 3, -std::log(0.5), ab_ba_state));  // aa ab ba 1
    fst.AddArc(aa_ab_state,
               StdArc(0, 0, -std::log(0.75), ab_state));  // backoff arc.
    fst.AddArc(aa_ba_state,
               StdArc(2, 2, -std::log(0.5), ba_ab_state));  // aa ba ab 1
    fst.AddArc(aa_ba_state,
               StdArc(0, 0, -std::log(0.75), ba_state));  // backoff arc.
    fst.AddArc(ab_ba_state,
               StdArc(4, 4, -std::log(0.5), ba_bbb_state));  // ab ba bbb 1
    fst.AddArc(ab_ba_state,
               StdArc(0, 0, -std::log(0.75), ba_state));  // backoff arc.
    fst.SetFinal(ab_bbb_state, -std::log(0.5));           // ab bbb </S>      1
    fst.AddArc(ab_bbb_state,
               StdArc(0, 0, -std::log(1.5), bbb_state));  // backoff arc.
    fst.AddArc(ba_ab_state,
               StdArc(4, 4, -std::log(0.5), ab_bbb_state));  // ba ab bbb 1
    fst.AddArc(ba_ab_state,
               StdArc(0, 0, -std::log(0.75), ab_state));  // backoff arc.
    fst.SetFinal(ba_bbb_state, -std::log(0.5));           // ba bbb </S>      1
    fst.AddArc(ba_bbb_state,
               StdArc(0, 0, -std::log(1.5), bbb_state));  // backoff arc.

    ArcSort(&fst, ILabelCompare<StdArc>());
    fst.SetInputSymbols(&syms);
    fst.SetOutputSymbols(&syms);
    fst.Write(trigram_model_file_);
  }

  void SetUp() override {
    // Setup the treegram count file.
    trigram_model_file_ = nisaba::file::TempFilePath("trigram_word_mod.fst");
    CreateFstWordTrigramModelFile();

    // Configure the model.
    storage_.set_model_file(trigram_model_file_);
  }

  std::string trigram_model_file_;  // File name of created trigram model FST.
  ModelStorage storage_;  // Default storage proto for initializing model;
};

// Calculating ContextState functionality.
TEST_F(NGramWordFstTest, ContextState) {
  NGramWordFstModel model;
  ASSERT_OK(model.Read(storage_));
  ASSERT_EQ(model.ContextState("aa "), 6);
}

// Calculating ContextState functionality for implicit states.
TEST_F(NGramWordFstTest, ContextStateImplicit) {
  NGramWordFstModel model;
  ASSERT_OK(model.Read(storage_));
  // There are 13 states in the model.  If we read in "aa b" then we reach the
  // fourth non-explicit state in the model, i.e., (1) the oov_state_, (2) after
  // the first a, (3) after the second a, and (4) after the b.  After the
  // whitespace, we reach a model state, so no implicit state is created.  Hence
  // we should be at the 17th state, i.e., state 16.
  ASSERT_EQ(model.ContextState("aa b"), 16);
}

// Calculating ContextState functionality for same implicit states.
TEST_F(NGramWordFstTest, ContextStateImplicitSame) {
  NGramWordFstModel model;
  ASSERT_OK(model.Read(storage_));
  // There are no explicit trigram states "<S> bbb" or "bbb bbb", hence both of
  // these should result in the explicit model state associated with "bbb b".
  ASSERT_EQ(model.ContextState("bbb b"), model.ContextState("bbb bbb b"));
}

// Getting probs through extract LM scores at start state.
TEST_F(NGramWordFstTest, ExtractLMScoresStart) {
  NGramWordFstModel model;
  ASSERT_OK(model.Read(storage_));
  int start_state = model.ContextState("");
  LMScores lm_scores;
  ASSERT_TRUE(model.ExtractLMScores(start_state, &lm_scores));

  // Probability of "aa" is 2/3 at the start state, and all other symbols
  // (including </S> are 1/12), hence words that start with the letter "a"
  // (i.e., "aa" and "ab") have probability 2/3 + 1/12 = 3/4; words that start
  // with the letter "b" (i.e., "ba" and "bbb") have probability 1/12 + 1/12 =
  // 1/6; and </S> has probability 1/12.
  std::vector<double> expected_probs = {0.0833333, 0.75, 0.16666667};
  std::vector<double> extracted_probs(3);
  ASSERT_EQ(lm_scores.probabilities_size(), 3);
  for (int i = 0; i < lm_scores.probabilities_size(); i++) {
    int idx = 0;
    double lm_score = 0.0;
    if (!lm_scores.symbols(i).empty()) {
      char32 utf8_code;
      ASSERT_TRUE(nisaba::utf8::DecodeSingleUnicodeChar(
          lm_scores.symbols(i), &utf8_code));
      // Offset converts from codepoint index for 'a' and 'b' to symbol idx.
      idx = static_cast<int>(utf8_code) - 96;
      lm_score = model.SymLMScore(start_state, static_cast<int>(utf8_code));
    } else {
      // End-of-string (</S>) is, by convention, index 0.
      lm_score = model.SymLMScore(start_state, /*utf8_code=*/0);
    }
    ASSERT_LT(idx, 3);
    ASSERT_GE(idx, 0);
    extracted_probs[idx] = lm_scores.probabilities(i);
    ASSERT_NEAR(exp(-lm_score), extracted_probs[idx], kFloatDelta);
  }
  for (int i = 0; i < 3; ++i) {
    ASSERT_NEAR(extracted_probs[i], expected_probs[i], kFloatDelta);
  }
}

// Getting probs through extract LM scores at implicit state with just one
// continuation.
TEST_F(NGramWordFstTest, ExtractLMScoresImplicitOneContinuation) {
  NGramWordFstModel model;
  ASSERT_OK(model.Read(storage_));
  int start_state = model.ContextState("aa bb");
  LMScores lm_scores;
  ASSERT_TRUE(model.ExtractLMScores(start_state, &lm_scores));

  // Since 'bb' is a prefix of only one word in the vocabulary, the only
  // possible continuation is "b", so everything else will have 0 probability.
  ASSERT_EQ(lm_scores.probabilities_size(), 1);
  ASSERT_EQ(lm_scores.symbols(0), "b");
  ASSERT_NEAR(lm_scores.probabilities(0), 1.0, kFloatDelta);
}

// Getting probs through extract LM scores at implicit state with just one
// continuation at a word boundary.
TEST_F(NGramWordFstTest, ExtractLMScoresImplicitWordBoundary) {
  NGramWordFstModel model;
  ASSERT_OK(model.Read(storage_));
  int start_state = model.ContextState("aa ba");
  LMScores lm_scores;
  ASSERT_TRUE(model.ExtractLMScores(start_state, &lm_scores));

  // Since 'ba' is a complete word, the prediction is word boundary. Some of
  // that probability mass goes to whitespace, some to end-of-string.  The
  // end-of-string probability is calculated by going to the next state and
  // deriving the final cost there.  After ba, we reach "aa_ba_state".  To reach
  // end of string from that state, we have to backoff to "ba_state" at a cost
  // of -log(3/4), then backoff again to the unigram state at a cost of
  // -log(5/9), where </S> has cost -log(1/5).  This leads to a probability of
  // ending the string of 1/12, and a probability of whitespace of 11/12.
  ASSERT_EQ(lm_scores.probabilities_size(), 2);
  int space_idx = lm_scores.symbols(0).empty() ? 1 : 0;
  int end_of_string_idx = 1 - space_idx;
  ASSERT_TRUE(lm_scores.symbols(end_of_string_idx).empty());
  ASSERT_EQ(lm_scores.symbols(space_idx), " ");
  ASSERT_NEAR(lm_scores.probabilities(space_idx), 11.0 / 12.0, kFloatDelta);
  ASSERT_NEAR(lm_scores.probabilities(end_of_string_idx), 1.0 / 12.0,
              kFloatDelta);
}

// Check that we can use the FSTs converted from third-party models.
//
// Note: We don't run this test on Windows because we presently cannot verify
// that Bazel genrules that generate FSTs from ARPA format work properly. This
// is because doing so requires configuring a Windows Linux Subsystem (WLS) for
// Bazel to get access to bash and other Unix utilities.
#if !defined(_MSC_VER)
TEST(NGramWordFstStandaloneTest, ThirdPartyModelTest) {
  // Third-party model from Michigan Tech (MTU).
  constexpr char kThirdPartyModelDir[] =
      "com_google_mozolm/third_party/models/mtu";
  constexpr char kThirdParty3GramModelName[] =
      "dasher_feb21_eng_word_5k_3gram.fst";
  ModelStorage model_storage;
  const std::string model_path = TestFilePath(kThirdPartyModelDir,
                                              kThirdParty3GramModelName);
  model_storage.set_model_file(model_path);
  NGramWordFstModel model;
  ASSERT_OK(model.Read(model_storage))
      << "Failed to read model from " << model_path;

  // Trivial 3-gram checks.
  std::pair<double, std::string> top_next;
  CheckTopCandidateForContext("four years a", &model, &top_next);
  EXPECT_EQ("g", top_next.second);
  CheckTopCandidateForContext("four years ag", &model, &top_next);
  EXPECT_EQ("o", top_next.second);
  CheckTopCandidateForContext("four years ago", &model, &top_next);
  EXPECT_EQ(" ", top_next.second);
}
#endif  // _MSC_VER

}  // namespace
}  // namespace models
}  // namespace mozolm
