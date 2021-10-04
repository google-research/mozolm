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

// Classes to support FST-based implementation of character-based PPM model.
//
// We follow the "blending" and "update exclusion" (known as 'single counting'
// from Moffat, 1990) approach taken in Steinruecken et al. (2015), citation
// below, and assign probabilities using a variant of equation 4 in that paper.
// In such an approach, there are three hyper-parameters: \alpha, \beta and
// max_order.  Both \alpha and \beta fall between 0 and 1, and max_order >= 0
// specifies the longest strings to include in the model.  For a max_order of k,
// the longest strings included in the model are of length k+1.
//
// Let \Sigma be a vocabulary of characters, including a special end-of-string
// symbol. Let h \in \Sigma^* be the contextual history and s \in \Sigma a
// symbol following h, e.g., h might be "this is the contextual histor" and s
// might be "y".  Let h' be the backoff contextual history for h, which is the
// longest proper suffix of h if one exists, and the empty string otherwise.
// Thus, for our example above, h' is "his is the contextual histor".  For any x
// \in \Sigma^* let c(x) denote the count of x, and C(x) = max(c(x) - \beta, 0).
// We will specify how counts are derived later. Let U(h) = { s : c(hs) > 0 }
// and S(h) = \sum_{x} c(hx).
//
// Probabilities are defined based on "blending" multiple orders, a calculation
// which recurses to lower orders, terminating at the unigram probability, which
// is when h = "" (the empty string). For the unigram probability, we smooth via
// add-one Laplace smoothing, i.e., P(s) = ( c(s) + 1 ) / \sum_{x} ( c(x) + 1 ).
// If h is non-empty, then its probability is defined as follows:
// P(s | h) = ( C(hs) + ( U(h)\beta + \alpha ) P(s | h') ) / ( S(h) + \alpha )
//
// All that is left to specify is how to count, which we do via "update
// exclusion".  With each new observation s in the context of h, we update our
// count c(hs).  Let k = min(length(hs), max_order+1), and let X=h's be the
// suffix of hs of length k.  Let X' be the longest suffix of X that was
// previously observed, i.e., where c(X') > 0. (We assume that s has been
// observed, since we use Laplace add-one smoothing for the unigram.)  Then we
// increment the counts by one for all substrings Y of hs such that
// length(X) >= length(Y) >= length(X').
//
// References:
// J. G. Cleary and I. H. Witten. 1984. Data compression using adaptive coding
// and partial string matching. IEEE Transactions on Communications,
// 32(4):396–402.
//
// A. Moffat. 1990. Implementing the PPM data compression scheme. IEEE
// Transactions on Communications, 38(11):1917–1921.
//
// C. Steinruecken, Z. Ghahramani and D. MacKay. 2015. Improving PPM with
// dynamic parameter updates. IEEE Data Compression Conference.

#ifndef MOZOLM_MOZOLM_MODELS_PPM_AS_FST_MODEL_H_
#define MOZOLM_MOZOLM_MODELS_PPM_AS_FST_MODEL_H_

#include <memory>
#include <string>
#include <vector>

#include "mozolm/stubs/integral_types.h"
#include "fst/symbol-table.h"
#include "fst/vector-fst.h"
#include "ngram/ngram-count.h"
#include "absl/status/statusor.h"
#include "mozolm/models/language_model.h"
#include "mozolm/models/model_storage.pb.h"
#include "mozolm/models/ppm_as_fst_options.pb.h"

namespace mozolm {
namespace models {

constexpr int kMaxCache = 2000;  // Maximum states to cache.
constexpr int kMaxLine = 51200;  // For reading text with FileLines.
constexpr float kAlpha = 0.5;    // Default \alpha parameter for PPM model.
constexpr float kBeta = 0.75;    // Default \beta parameter for PPM model.
constexpr int kMaxOrder = 4;     // Default max_order parameter for PPM model.

// State information caching class.
class PpmStateCache {
 public:
  PpmStateCache() = default;

  explicit PpmStateCache(int state) : state_(state) {
    last_accessed_ = -1;
    last_updated_ = -1;
  }

  // Updates cache information.
  void UpdateCache(int access_counter,
                   const std::vector<int>& arc_origin_states,
                   const std::vector<int>& destination_states,
                   const std::vector<double>& neg_log_probabilities,
                   double normalization);

  // Updates cache with values from provided cache entry.
  void UpdateCache(int access_counter, const PpmStateCache& state_cache);

  // Returns state associated with this cache.
  int state() const { return state_; };

  // Returns index of last time accessed.
  int last_accessed() const { return last_accessed_; };

  // Returns index of last time accessed.
  int last_updated() const { return last_updated_; };

  // Returns the size of the cached vectors.
  int VectorSize() const { return destination_states_.size(); };

  // Verifies sym_index within range.
  absl::Status VerifyAccess(int sym_index, size_t vector_size) const;

  // Returns the cached arc origin state for given sym_index.
  absl::StatusOr<int> ArcOriginState(int sym_index) const;

  // Returns all cached arc origin states.
  std::vector<int> arc_origin_states() const { return arc_origin_states_; }

  // Returns the cached destination state for given sym_index.
  absl::StatusOr<int> DestinationState(int sym_index) const;

  // Returns all cached destination states.
  std::vector<int> destination_states() const { return destination_states_; }

  // Returns the cached neg_log_probability for given sym_index.
  absl::StatusOr<double> NegLogProbability(int sym_index) const;

  // Returns all cached negative log probabilities.
  std::vector<double> neg_log_probabilities() const {
    return neg_log_probabilities_;
  }

  // Returns the normalization from the state.
  double normalization() const { return normalization_; }

  // Updates the last_accessed_ index.
  void set_last_accessed(int access_counter) {
    last_accessed_ = access_counter;
  }

  // Fills in LMScores proto from values in cached state.
  bool FillLMScores(const fst::SymbolTable& syms, LMScores* response) const;

 private:
  int state_;                           // Index of state being cached.
  int last_accessed_;                   // Stores index of last time accessed.
  int last_updated_;                    // Stores index of last time updated.
  std::vector<int> arc_origin_states_;  // State originating arc with sym_index.
  std::vector<int> destination_states_;        // Cache of destination states.
  std::vector<double> neg_log_probabilities_;  // Cache of weights.
  double normalization_;  // Denominator in normalization for calculating probs.
};

// PPM class using FST-based counts.
class PpmAsFstModel : public LanguageModel {
 public:
  PpmAsFstModel() = default;
  ~PpmAsFstModel() override = default;

  absl::Status Read(const ModelStorage& storage) override;

  // Writes fst_ model to file.
  absl::Status WriteFst(const std::string& ofile);

  // Returns fst_.
  const fst::StdVectorFst GetFst() const { return *fst_; }

  // Provides the state reached from state following utf8_sym.
  int NextState(int state, int utf8_sym) override;

  // Copies the probs and normalization from the given state into the response.
  bool ExtractLMScores(int state, LMScores* response) override;

  // Updates the counts for the utf8_syms at the current state.
  bool UpdateLMCounts(int32 state, const std::vector<int>& utf8_syms,
                      int64 count) override;

  // Converts string to vector of symbol table indices. Requires sticking to
  // allowed symbols.
  absl::StatusOr<std::vector<int>> GetSymsVector(
      const std::string& input_string) {
    return GetSymsVector(input_string, /*add_sym=*/false);
  }

  // Returns probabilities of vector of symbols, treated as string.  Converts to
  // bits (base 2) if bool argument is set to true; otherwise nats (base e).
  absl::StatusOr<std::vector<double>> GetNegLogProbs(
      const std::vector<int>& sym_indices, bool return_bits = false);

 private:
  // Trains fst model from vector of strings.
  absl::Status TrainFromText(const std::vector<std::string>& istrings);

  // Converts string to vector of symbol table indices.
  absl::StatusOr<std::vector<int>> GetSymsVector(
      const std::string& input_string, bool add_sym);

  // Initializes Fst class from options.
  void InitFst(const PpmAsFstOptions& ppm_as_fst_config);

  // Calculates the state order for given state, using backoffs.
  absl::StatusOr<int> CalculateStateOrder(int s);

  // Calculates and stores state orders for updates to model.  Updates
  // max_order_ of model if higher than provided parameter.
  absl::Status CalculateStateOrders(bool save_state_orders);

  // Determines whether new state needs to be created for arc.
  absl::StatusOr<bool> NeedsNewState(fst::StdArc::StateId curr_state,
                                     fst::StdArc::StateId next_state) const;

  // Adds extra characters to unigram of model if provided.
  absl::Status AddExtraCharacters(const std::string& input_string);

  // Fills in cache vectors of negative log probabilities and destination states
  // for each item in the vocabulary, matching indices with the symbol table. By
  // convention, index 0 is for final cost.  Checks for empty states and ensures
  // backoff states are cached.
  absl::Status UpdateCacheAtState(fst::StdArc::StateId s);

  // Initializes negative log probabilities for cache based on backoff.
  std::vector<double> InitCacheProbs(fst::StdArc::StateId s,
                                     fst::StdArc::StateId backoff_state,
                                     const PpmStateCache& backoff_cache,
                                     double denominator) const;

  // Initializes the origin and destination states for cache based on backoff.
  std::vector<int> InitCacheStates(fst::StdArc::StateId s,
                                   fst::StdArc::StateId backoff_state,
                                   const PpmStateCache& backoff_cache,
                                   bool arc_origin) const;

  // Fills in values for states and probs vectors from state for cache.
  absl::Status UpdateCacheStatesAndProbs(
      fst::StdArc::StateId s, fst::StdArc::StateId backoff_state,
      double denominator, std::vector<int>* arc_origin_states,
      std::vector<int>* destination_states,
      std::vector<double>* neg_log_probabilities);

  // Fills in cache vectors of negative log probabilities and destination states
  // for each item in the vocabulary, matching indices with the symbol table. By
  // convention, index 0 is for final cost.
  absl::Status UpdateCacheAtNonEmptyState(
      fst::StdArc::StateId s, fst::StdArc::StateId backoff_state,
      const PpmStateCache& backoff_cache);

  // Checks if lower order state caches have updated more recently.
  bool LowerOrderCacheUpdated(fst::StdArc::StateId s) const;

  // Ensures cache exists for state, creates it if not.
  absl::StatusOr<PpmStateCache> EnsureCacheAtState(fst::StdArc::StateId s);

  // Finds cache entry with oldest last access, for replacement.
  int FindOldestLastAccessedCache() const;

  // Establishes cache index, after performing garbage collection if needed.
  absl::Status GetNewCacheIndex(fst::StdArc::StateId s);

  // Adds new state to all required data structures and returns index.
  absl::StatusOr<int> AddNewState(fst::StdArc::StateId backoff_dest_state);

  // Returns origin state of arc with symbol from state s.
  absl::StatusOr<int> GetArcOriginState(fst::StdArc::StateId s,
                                        int sym_index);

  // Returns destination state of arc with symbol from state s.
  absl::StatusOr<int> GetDestinationState(fst::StdArc::StateId s,
                                          int sym_index);

  // Returns probability of symbol leaving the current state.
  absl::StatusOr<double> GetNegLogProb(fst::StdArc::StateId s,
                                       int sym_index);

  // Returns normalization value at the current state.
  absl::StatusOr<double> GetNormalization(fst::StdArc::StateId s);

  // Updates model at highest found state for given symbol.
  absl::Status UpdateHighestFoundState(fst::StdArc::StateId curr_state,
                                       int sym_index);

  // Updates model at state where given symbol is not found.
  absl::Status UpdateNotFoundState(fst::StdArc::StateId curr_state,
                                   fst::StdArc::StateId highest_found_state,
                                   fst::StdArc::StateId backoff_state,
                                   int sym_index);

  // Updates model with an observation of the sym_index at curr_state.
  absl::StatusOr<fst::StdArc::StateId> UpdateModel(
      fst::StdArc::StateId curr_state,
      fst::StdArc::StateId highest_found_state, int sym_index);

  // Converts input string into linear FST at the character level, replacing
  // characters not in possible_characters_ set (if non-empty) with kOovSymbol.
  absl::StatusOr<fst::StdVectorFst> String2Fst(
      const std::string& input_string);

  // Adds a single unigram count to every character.
  absl::Status AddPriorCounts();

  int max_order_;      // Maximum n-gram order of the model.
  double alpha_;       // Alpha hyper-parameter for PPM.
  double beta_;        // Beta hyper-parameter for PPM.
  bool static_model_;  // Whether to use the model as static or dynamic.
  std::vector<int> state_orders_;  // Stores the order of each state.
  std::unique_ptr<fst::StdVectorFst> fst_;  // Model (counts) stored in FST.
  // For counting character n-grams if training from text file.
  std::unique_ptr<ngram::NGramCounter<fst::Log64Weight>> ngram_counter_;
  std::unique_ptr<fst::SymbolTable> syms_;  // Character symbols.

  // For caching probabilities and destination states for quick access.
  int max_cache_size_;  // Limit on caching for garbage collection.
  int cache_accessed_;  // Counter of cache accesses to determine priority.
  std::vector<int> cache_index_;  // Index of cache for state if it exists.
  std::vector<PpmStateCache> state_cache_;  // Cache for state information.
};

}  // namespace models
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_MODELS_PPM_AS_FST_MODEL_H_
