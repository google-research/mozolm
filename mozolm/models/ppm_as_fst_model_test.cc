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

#include "mozolm/models/ppm_as_fst_model.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fst/arcsort.h"
#include "fst/isomorphic.h"
#include "fst/symbol-table.h"
#include "fst/vector-fst.h"
#include "gmock/gmock.h"
#include "nisaba/port/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "mozolm/models/model_storage.pb.h"
#include "nisaba/port/utf8_util.h"

namespace mozolm {
namespace models {

static constexpr float kFloatDelta = 0.00001;  // Delta for float comparisons.

using ::nisaba::utf8::DecodeSingleUnicodeChar;
using ::fst::ArcSort;
using ::fst::ILabelCompare;
using ::fst::Isomorphic;
using ::fst::StdArc;
using ::fst::StdVectorFst;
using ::fst::SymbolTable;

class PpmAsFstTest : public ::testing::Test {
 protected:
  // Creates FST trigram count file, to test FST model initialization.
  void CreateFstTrigramCountFile() {
    SymbolTable syms;
    syms.AddSymbol("<epsilon>");
    syms.AddSymbol("a");
    syms.AddSymbol("b");

    // Creates trigram count file in PPM format from 2-line corpus:
    // abaab
    // aabab
    // which results in the following non-backed-off-to n-grams (and counts):
    // a a b     2
    // a b a     2
    // a b </S>  2
    // b a a     1
    // b a b     1
    // <S> a     2
    // <S> a a   1
    // <S> a b   1
    // where <S> is begin-of-string and </S> is end-of-string.

    // For  backed-off-to n-grams, we just count the number of higher order
    // n-grams for which it is the longest proper suffix.  Additionally,
    // unigrams have an extra count (Laplace smoothing). Thus:
    // a       4
    // a a     2
    // a b     3
    // b       2
    // b a     1
    // b </S>  1
    // </S>    2
    // We now create states for every n-gram prefix.
    StdVectorFst fst;
    // Order 0 state:
    int unigram_state = fst.AddState();
    // Order 1 states:
    int start_state = fst.AddState();
    fst.SetStart(start_state);
    int a_state = fst.AddState();
    int b_state = fst.AddState();
    // Order 2 states:
    int sa_state = fst.AddState();
    int aa_state = fst.AddState();
    int ab_state = fst.AddState();
    int ba_state = fst.AddState();
    // Add arcs with -log(counts) for each n-gram from proper prefix state to
    // suffix state -- proper suffix if not ascending.  Also add epsilon backoff
    // arcs to backoff states with -log sum of counts on that arc.  N-grams with
    // </S> go to final cost rather than arcs.
    fst.AddArc(aa_state, StdArc(2, 2, -log(2.0), ab_state));  // a a b     2
    fst.AddArc(aa_state, StdArc(0, 0, -log(2.0), a_state));
    fst.AddArc(ab_state, StdArc(1, 1, -log(2.0), ba_state));     // a b a     2
    fst.SetFinal(ab_state, -log(2.0));                           // a b </S>  2
    fst.AddArc(ab_state, StdArc(0, 0, -log(4.0), b_state));
    fst.AddArc(ba_state, StdArc(1, 1, -log(1.0), aa_state));     // b a a     1
    fst.AddArc(ba_state, StdArc(2, 2, -log(1.0), ab_state));     // b a b     1
    fst.AddArc(ba_state, StdArc(0, 0, -log(2.0), a_state));
    fst.AddArc(start_state, StdArc(1, 1, -log(2.0), sa_state));  // <S> a     2
    fst.AddArc(start_state, StdArc(0, 0, -log(2.0), unigram_state));
    fst.AddArc(sa_state, StdArc(1, 1, -log(1.0), aa_state));     // <S> a a   1
    fst.AddArc(sa_state, StdArc(2, 2, -log(1.0), ab_state));     // <S> a b   1
    fst.AddArc(sa_state, StdArc(0, 0, -log(2.0), a_state));
    fst.AddArc(a_state, StdArc(1, 1, -log(2.0), aa_state));       // a a     2
    fst.AddArc(a_state, StdArc(2, 2, -log(3.0), ab_state));       // a b     3
    fst.AddArc(a_state, StdArc(0, 0, -log(5.0), unigram_state));
    fst.AddArc(b_state, StdArc(1, 1, -log(1.0), ba_state));       // b a     1
    fst.SetFinal(b_state, -log(1.0));                             // b </S>  1
    fst.AddArc(b_state, StdArc(0, 0, -log(2.0), unigram_state));
    fst.AddArc(unigram_state, StdArc(1, 1, -log(4.0), a_state));  // a       4
    fst.AddArc(unigram_state, StdArc(2, 2, -log(2.0), b_state));  // b       2
    fst.SetFinal(unigram_state, -log(2.0));                       // </S>    2
    ArcSort(&fst, ILabelCompare<StdArc>());
    fst.SetInputSymbols(&syms);
    fst.SetOutputSymbols(&syms);
    fst.Write(trigram_count_file_);
  }

  // Creates corpus file for testing text file model initialization.
  void CreateCorpusFile() {
    std::ofstream output_file(corpus_file_);
    ASSERT_TRUE(output_file.good()) << "Failed to open: " << corpus_file_;
    output_file << absl::StrCat("abaab", "\n");
    output_file << absl::StrCat("aabab", "\n");
    ASSERT_TRUE(output_file.good()) << "Failed to write to " << corpus_file_;
  }

  void SetUp() override {
    // Setup the treegram count file.
    const std::filesystem::path tmp_dir =
        std::filesystem::temp_directory_path();
    std::filesystem::path file_path = tmp_dir / "trigram_count.fst";
    trigram_count_file_ = file_path.string();
    CreateFstTrigramCountFile();

    // Setup the training set.
    file_path = tmp_dir / "corpus.txt";
    corpus_file_ = file_path.string();
    CreateCorpusFile();

    // Configure the model.
    max_order_ = 3;
    storage_.set_model_file(trigram_count_file_);
    storage_.mutable_ppm_options()->set_max_order(max_order_);
    storage_.mutable_ppm_options()->set_static_model(true);
    storage_.mutable_ppm_options()->set_model_is_fst(true);
  }

  int max_order_;                   // Max order of model.
  std::string trigram_count_file_;  // File name of created trigram count FST.
  std::string corpus_file_;         // File name of corpus text file.
  ModelStorage storage_;  // Default storage proto for initializing model;
};

// Initializing with either corpus or FST yields same result for static model.
TEST_F(PpmAsFstTest, InitializingFromFstOrTextTheSame) {
  PpmAsFstModel model_from_fst;
  ModelStorage storage_fst = storage_;
  ASSERT_OK(model_from_fst.Read(storage_fst));
  PpmAsFstModel model_from_text;
  ModelStorage storage_file = storage_;
  storage_file.set_model_file(corpus_file_);
  storage_file.mutable_ppm_options()->set_model_is_fst(false);
  ASSERT_OK(model_from_text.Read(storage_file));
  ASSERT_TRUE(
      Isomorphic<StdArc>(model_from_fst.GetFst(), model_from_text.GetFst()));
}

// Initializing with either corpus or FST yields same result for dynamic model.
TEST_F(PpmAsFstTest, InitializingFromFstOrTextTheSameDynamic) {
  PpmAsFstModel model_from_fst;
  ModelStorage storage_fst = storage_;
  storage_fst.mutable_ppm_options()->set_static_model(false);
  ASSERT_OK(model_from_fst.Read(storage_fst));
  PpmAsFstModel model_from_text;
  ModelStorage storage_file = storage_;
  storage_file.set_model_file(corpus_file_);
  storage_file.mutable_ppm_options()->set_static_model(false);
  storage_file.mutable_ppm_options()->set_model_is_fst(false);
  ASSERT_OK(model_from_text.Read(storage_file));
  const std::string test_string = "babbbabababba";
  auto sym_indices_status = model_from_fst.GetSymsVector(test_string);
  ASSERT_TRUE(sym_indices_status.ok());
  std::vector<int> sym_indices_a = sym_indices_status.value();
  // Adds end-of-string default string ("</S>") and index (0) to vectors.
  sym_indices_a.push_back(0);
  auto neg_log_probs_status = model_from_fst.GetNegLogProbs(sym_indices_a);
  ASSERT_TRUE(neg_log_probs_status.ok());
  sym_indices_status = model_from_text.GetSymsVector(test_string);
  ASSERT_TRUE(sym_indices_status.ok());
  std::vector<int> sym_indices_b = sym_indices_status.value();
  // Adds end-of-string default string ("</S>") and index (0) to vectors.
  sym_indices_b.push_back(0);
  neg_log_probs_status = model_from_text.GetNegLogProbs(sym_indices_b);
  ASSERT_TRUE(neg_log_probs_status.ok());
  ASSERT_TRUE(
      Isomorphic<StdArc>(model_from_fst.GetFst(), model_from_text.GetFst()));
}

// Static model probabilities match hand calculations.
TEST_F(PpmAsFstTest, StaticProbsMatchHand) {
  PpmAsFstModel model;
  ModelStorage storage = storage_;
  ASSERT_OK(model.Read(storage));
  const std::string test_string = "bab";
  auto sym_indices_status = model.GetSymsVector(test_string);
  ASSERT_TRUE(sym_indices_status.ok());
  std::vector<int> sym_indices = sym_indices_status.value();
  // Adds end-of-string default string ("</S>") and index (0) to vectors.
  sym_indices.push_back(0);
  auto neg_log_probs_status = model.GetNegLogProbs(sym_indices);
  ASSERT_TRUE(neg_log_probs_status.ok());
  const std::vector<double> neg_log_probs = neg_log_probs_status.value();
  ASSERT_EQ(neg_log_probs.size(), 4);
  // Calculate each probability, through equation:
  // P(s | h) = (\hat{c}(hs))/D + \gamma P(s | h')
  // where \hat{c}(hs) = c(hs) - \beta if c(hs) > 0, 0 otherwise and
  // D = c(h) + \alpha and \gamma = (|S|\beta + \alpha)/D where
  // |S| is the size of the set S = {s : c(hs) > 0}.
  // Apart from Laplace smoothing, unigrams are just relative frequency, so
  // P(a) = 0.5; P(b) = 0.25; P(</S>) = 0.25.
  // Then P(b | <S>) = 0 / D + (kBeta + kAlpha) P(b) / D
  //                 = (0.5 + 0.75) 0.25 / (2 + 0.5) = 0.125.
  ASSERT_NEAR(neg_log_probs[0], -log(0.125), kFloatDelta);

  // Next P(a | <S>b) = P(a | b) since c(<S>b) == 0.
  // P(a | b) = (c(ba) - kBeta) / D + (2 kBeta + kAlpha) P(a) / D
  //          = (1 - 0.75) / (2 + 0.5) + (2 x 0.75 + 0.5) 0.5 / (2 + 0.5)
  //          = 0.1 + 0.4 = 0.5.
  ASSERT_NEAR(neg_log_probs[1], -log(0.5), kFloatDelta);

  // Next P(b | ba) = (c(bab) - kBeta) / D_0 + (2 kBeta + kAlpha) P(b | a) / D_0
  // where P(b | a) = (c(ab) - kBeta) / D_1 + (2 kBeta + kAlpha) P(b) / D_1
  //                = (3 - 0.75) / (5 + 0.5) + (2 x 0.75 + 0.5) 0.25 / (5 + 0.5)
  //                = 0.409090909 + 0.0909090909 = 0.5
  // Hence P(b | ba) = (1 - 0.75) / (2 + 0.5) + (2 x 0.75 + 0.5) 0.5 / (2 + 0.5)
  //                 = 0.1 + 0.4 = 0.5
  ASSERT_NEAR(neg_log_probs[2], -log(0.5), kFloatDelta);

  // Finally,
  // P(</S> | ab) = (c(ab</S>) - kBeta)/D_0 + (2 kBeta + kAlpha) P(</S> | b)/D_0
  // where P(</S> | b) = (c(b</S>) - kBeta)/D_1 + (2 kBeta + kAlpha) P(</S>)/D_1
  //                   = (1 - 0.75)/(2 + 0.5) + (2 x 0.75 + 0.5) 0.25/(2 + 0.5)
  //                   = 0.1 + 0.2 = 0.3
  // Hence P(</S> | ab) = (2 - 0.75)/(4 + 0.5) + (2 x 0.75 + 0.5) 0.3/(4 + 0.5)
  //                    = 0.2777777 + 0.133333333 = 0.41111111
  ASSERT_NEAR(neg_log_probs[3], -log(0.41111111), kFloatDelta);
}

// Dynamic model probabilities match hand calculations.
TEST_F(PpmAsFstTest, DynamicProbsMatchHand) {
  PpmAsFstModel model;
  ModelStorage storage = storage_;
  storage.mutable_ppm_options()->set_static_model(false);
  ASSERT_OK(model.Read(storage));
  const std::string test_string = "bab";
  auto sym_indices_status = model.GetSymsVector(test_string);
  ASSERT_TRUE(sym_indices_status.ok());
  std::vector<int> sym_indices = sym_indices_status.value();
  // Adds end-of-string default string ("</S>") and index (0) to vectors.
  sym_indices.push_back(0);
  auto neg_log_probs_status = model.GetNegLogProbs(sym_indices);
  ASSERT_TRUE(neg_log_probs_status.ok());
  const std::vector<double> neg_log_probs = neg_log_probs_status.value();
  ASSERT_EQ(neg_log_probs.size(), 4);
  // For the initial observation, the result is same as static model:
  ASSERT_NEAR(neg_log_probs[0], -log(0.125), kFloatDelta);

  // Next P(a | <S>b) = P(a | b) since c(<S>b) == 0. (Still true, no
  // continuation observations.)
  // Unigram counts have been updated, with one extra b count, so
  // P(a) = 4/9; P(b) = 1/3; P(</S>) = 2/9.
  // All other relevant dynamic counts have remained the same, hence:
  // P(a | b) = (c(ba) - kBeta) / D + (2 kBeta + kAlpha) P(a) / D
  //          = (1 - 0.75) / (2 + 0.5) + (2 x 0.75 + 0.5) 0.444444 / (2 + 0.5)
  //          = 0.1 + 0.3555555 = 0.455555555.
  ASSERT_NEAR(neg_log_probs[1], -log(0.455555555), kFloatDelta);

  // None of the count updates for the previous step impact this, so just the
  // updated unigrams from the earlier dynamic count.
  // Next P(b | ba) = (c(bab) - kBeta) / D_0 + (2 kBeta + kAlpha) P(b | a) / D_0
  // where P(b | a) = (c(ab) - kBeta) / D_1 + (2 kBeta + kAlpha) P(b) / D_1
  //                = (3 - 0.75)/(5 + 0.5) + (2 x 0.75 + 0.5) 0.333/(5 + 0.5)
  //                = 0.409090909 + 0.1212121212 = 0.53030303
  // Thus P(b | ba) = (1 - 0.75)/(2 + 0.5) + (2 x 0.75 + 0.5) 0.530303/(2 + 0.5)
  //                 = 0.1 + 0.4242424 = 0.5242424
  ASSERT_NEAR(neg_log_probs[2], -log(0.52424242424), kFloatDelta);

  // Finally, the count c(ba) updated by 1 earlier in the string, since <S>ba is
  // a new trigram backing off to it, so this bigram distribution is updated.
  // P(</S> | ab) = (c(ab</S>) - kBeta)/D_0 + (2 kBeta + kAlpha) P(</S> | b)/D_0
  // where P(</S> | b) = (c(b</S>) - kBeta)/D_1 + (2 kBeta + kAlpha) P(</S>)/D_1
  //                   = (1 - 0.75)/(3 + 0.5) + (2 x 0.75 + 0.5) 0.222/(3 + 0.5)
  //                   = 0.0714286 + 0.126984 = 0.1984127
  // Hence P(</S> | ab) = (2 - 0.75)/4.5 + (2 x 0.75 + 0.5) 0.1984127/4.5
  //                    = 0.2777777 + 0.088183422 = 0.3659612
  ASSERT_NEAR(neg_log_probs[3], -log(0.3659612), kFloatDelta);
}

// Calculating ContextState functionality.
TEST_F(PpmAsFstTest, ContextState) {
  PpmAsFstModel model_from_fst;
  ModelStorage storage_fst = storage_;
  ASSERT_OK(model_from_fst.Read(storage_fst));
  ASSERT_EQ(model_from_fst.ContextState("aabababababababa"), 7);
}

// Getting probs through extract LM scores.
TEST_F(PpmAsFstTest, ExtractLMScores) {
  PpmAsFstModel model;
  ModelStorage storage = storage_;
  ASSERT_OK(model.Read(storage));
  int start_state = model.ContextState("");
  LMScores lm_scores;
  ASSERT_TRUE(model.ExtractLMScores(start_state, &lm_scores));

  // Probability of "b" and </S> at the start state are equiprobable (same
  // number of observations at the unigram state, not seen at the start state).
  // We see from the StaticProbsMatchHand test above that this is 0.125, hence
  // the remaining 0.75 must go to "a".
  std::vector<double> expected_probs = {0.125, 0.75, 0.125};
  std::vector<double> extracted_probs(3);
  ASSERT_EQ(lm_scores.probabilities_size(), 3);
  for (int i = 0; i < lm_scores.probabilities_size(); i++) {
    int idx = 0;
    if (!lm_scores.symbols(i).empty()) {
      char32 utf8_code;
      ASSERT_TRUE(DecodeSingleUnicodeChar(lm_scores.symbols(i), &utf8_code));
      // Offset converts from codepoint index for 'a' and 'b' to symbol idx.
      idx = static_cast<int>(utf8_code) - 96;
    }
    ASSERT_LT(idx, 3);
    ASSERT_GE(idx, 0);
    extracted_probs[idx] = lm_scores.probabilities(i);
  }
  for (int i = 0; i < 3; ++i) {
    ASSERT_NEAR(extracted_probs[i], expected_probs[i], kFloatDelta);
  }
}

// Updating probs through UpdateLMCounts.
TEST_F(PpmAsFstTest, UpdateLMCounts) {
  PpmAsFstModel model;
  ModelStorage storage = storage_;
  storage.mutable_ppm_options()->set_static_model(false);
  ASSERT_OK(model.Read(storage));
  const int start_state = model.ContextState("");

  // Add a single count at the start state for both "b" and </S>. Since these
  // are unobserved, the start state count goes to 1 for each and the unigram
  // count increases by 1 for each; they remain equiprobable at that state.
  ASSERT_TRUE(
      model.UpdateLMCounts(start_state, {98}, 1));  // updates "b" count.
  ASSERT_TRUE(
      model.UpdateLMCounts(start_state, {0}, 1));  // updates </S> count.
  LMScores lm_scores;
  ASSERT_TRUE(model.ExtractLMScores(start_state, &lm_scores));

  // Probability of "b" and </S> at the start state are equiprobable (same
  // number of observations at the unigram state, not seen at the start state).
  // Unigram probs with extra observations become: P(a)=0.4, P(b)=P(<S>)=0.3.
  // P(b | <S>) = (c(<S>b) - kBeta) / D + (3 kBeta + kAlpha) P(b) / D
  //          = (1 - 0.75) / (4 + 0.5) + (3 x 0.75 + 0.5) 0.3 / (4 + 0.5)
  //          = (0.25 + 0.825) / 4.5 = 0.2388888888.
  // P(</S> | <S>) = P(b | <S>) = 0.2388888888.
  // P(a | <S>) = 1.0 - P(b | <S>) - P(</S> | <S>) = 0.5222222222.
  std::vector<double> expected_probs = {0.23888888, 0.52222222, 0.23888888};
  std::vector<double> extracted_probs(3);
  ASSERT_EQ(lm_scores.probabilities_size(), 3);
  for (int i = 0; i < lm_scores.probabilities_size(); i++) {
    int idx = 0;
    if (!lm_scores.symbols(i).empty()) {
      char32 utf8_code;
      ASSERT_TRUE(DecodeSingleUnicodeChar(lm_scores.symbols(i), &utf8_code));
      // Offset converts from codepoint index for 'a' and 'b' to symbol idx.
      idx = static_cast<int>(utf8_code) - 96;
    }
    ASSERT_LT(idx, 3);
    ASSERT_GE(idx, 0);
    extracted_probs[idx] = lm_scores.probabilities(i);
    GOOGLE_LOG(WARNING) << "extracted " << idx << " " << extracted_probs[idx];
  }
  for (int i = 0; i < 3; ++i) {
    ASSERT_NEAR(extracted_probs[i], expected_probs[i], kFloatDelta);
  }
}

// Updating probs through UpdateLMCounts for a model initialized without
// training data or vocabulary.
TEST(PpmAsFstOtherTest, UpdateLMCountsForEmptyModel) {
  // Set up configuration for an empty initial model.
  ModelStorage storage;
  const int max_order = 3;
  storage.mutable_ppm_options()->set_max_order(max_order);
  storage.mutable_ppm_options()->set_static_model(false);
  storage.mutable_ppm_options()->set_model_is_fst(false);

  // Initialize the empty FST.
  PpmAsFstModel model;
  ASSERT_OK(model.Read(storage));

  // Check the initial model estimates. These only include the estimates for
  // the single end-of-sentence symbol.
  LMScores lm_scores;
  const int start_state = model.ContextState("");
  ASSERT_TRUE(model.ExtractLMScores(start_state, &lm_scores));
  ASSERT_EQ(1, lm_scores.symbols().size());
  EXPECT_TRUE(lm_scores.symbols()[0].empty());  // End-of-sentence.
  ASSERT_EQ(1, lm_scores.probabilities().size());
  EXPECT_EQ(1.0, lm_scores.probabilities()[0]);

  // Update the model with single observations for `a` and `b`.
  //
  // TODO: The returned LM symbols are clearly wrong. Fix.
  EXPECT_TRUE(model.UpdateLMCounts(start_state, {97}, 1));
  EXPECT_TRUE(model.UpdateLMCounts(start_state, {98}, 1));
  ASSERT_TRUE(model.ExtractLMScores(start_state, &lm_scores));
  ASSERT_EQ(2, lm_scores.symbols().size());
  EXPECT_EQ("", lm_scores.symbols()[0]);
  EXPECT_EQ("", lm_scores.symbols()[1]);
}

}  // namespace models
}  // namespace mozolm
