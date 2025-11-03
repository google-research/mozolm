// Copyright 2025 MozoLM Authors.
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

// Word n-gram model in OpenFst format served by OpenGrm NGram library.

#ifndef MOZOLM_MOZOLM_MODELS_NGRAM_WORD_FST_MODEL_H_
#define MOZOLM_MOZOLM_MODELS_NGRAM_WORD_FST_MODEL_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mozolm/models/ngram_fst_model.h"
#include "fst/vector-fst.h"

namespace mozolm {
namespace models {

constexpr int kMaxNGramCache = 2000;  // Maximum states to cache.

// Class for managing implicit states of model. The word-based model has
// explicit states for ngram contexts, but not for prefixes of possible words
// leaving those explicit states, which we will call implicit states. Each
// implicit state is associated with a specific explicit ngram context model
// state, a word prefix length, a symbol index corresponding to the first symbol
// matching the specific prefix at that state, and a symbol index corresponding
// to the final symbol matching the specific prefix at that state.  A flat hash
// map permits finding an existing state index from the associated tuple.
class NGramImplicitStates {
 public:
  NGramImplicitStates() = default;

  NGramImplicitStates(const fst::StdVectorFst& fst, int first_char_begin_index,
                      int first_char_end_index);

  // Returns the state if already exists, creates it otherwise.
  absl::StatusOr<int> GetState(int model_state, int prefix_length,
                               int symbol_begin_index, int symbol_end_index);

  // Returns the state if already exists, otherwise -1.
  int FindExistingState(int model_state, int prefix_length,
                        int symbol_begin_index);

  // Returns the model state associated with given implicit state.
  absl::StatusOr<int> model_state(int state) const;

  // Returns the prefix length for the given implicit state.
  absl::StatusOr<int> prefix_length(int state) const;

  // Returns the sybmol begin index for the given implicit state.
  absl::StatusOr<int> symbol_begin_index(int state) const;

  // Returns the symbol end index for the given implicit state.
  absl::StatusOr<int> symbol_end_index(int state) const;

 private:
  // Adds a new state with these indices.
  absl::StatusOr<int> AddNewState(int model_state, int prefix_length,
                                  int symbol_begin_index, int symbol_end_index);

  // Returns the vector index of the implicit state.
  absl::StatusOr<int> GetImplicitIdx(int state) const;

  // Returns the index for an associated prefix length.
  absl::StatusOr<int> GetPrefixIdx(int prefix_length);

  int explicit_model_states_;  // Number of explicit states in model.
  int total_model_states_;     // Number of implicit and explicit states.
  int max_prefix_length_;      // Maximum length in the symbol table.
  std::vector<int> model_state_;         // Model state for the implicit state.
  std::vector<int> prefix_length_;       // Length of symbol prefix at state.
  std::vector<int> symbol_begin_index_;  // First symbol index matching prefix.
  std::vector<int> symbol_end_index_;    // Last symbol index matching prefix.
  int explicit_state_begin_index_;  // Symbol begin index for explicit states.
  int explicit_state_end_index_;    // Symbol end index for explicit states.

  // Hashing to allow state index associated with word_initial_state_ and
  // symbol_begin_index_ for a given prefix_length_.
  std::vector<absl::flat_hash_map<std::pair<int, int>, int>>
      prefix_length_implicit_state_map_;
};

// State information caching class. This only caches information for explicit
// model states, and collects probabilities for each word in the whole
// vocabulary in lexicographic order.  Further, for easy aggregation over ranges
// of symbols associated with a prefix, we store the cummulative negative log
// probability for all words up to and including that index.  As a result, the
// probability for a range is the difference between the cummulative probability
// of the final element and the cummulative probability of the element preceding
// the initial element.  Because this is stored densely over the whole
// vocabulary, only some limited parameterized number of them are maintained in
// the cache.
class NGramStateCache {
 public:
  NGramStateCache() = default;

  NGramStateCache(int state, int access_counter,
                  const std::vector<double>& arc_weights)
      : state_(state),
        last_accessed_(access_counter),
        cummulative_neg_log_probs_(arc_weights) {}

  // Returns state associated with this cache.
  int state() const { return state_; }

  // Returns index of last time accessed.
  int last_accessed() const { return last_accessed_; }

  // Updates the last_accessed_ index.
  void set_last_accessed(int access_counter) {
    last_accessed_ = access_counter;
  }

  // Returns the value for the particular index if valid; Zero otherwise.
  double cummulative_neg_log_prob(int idx) const;

  std::vector<double> cummulative_neg_log_probs() const {
    return cummulative_neg_log_probs_;
  }

 private:
  int state_;                   // Index of model state being cached.
  int last_accessed_;           // Stores index of last time accessed.

  // This vector is the same size as the base model symbol table, and is for
  // lexicographically sorted symbols.  The probabilities are cummulative, so
  // that the probability for a range can be determined by taking a difference.
  std::vector<double> cummulative_neg_log_probs_;
};

class NGramWordFstModel : public NGramFstModel {
 public:
  NGramWordFstModel() = default;
  ~NGramWordFstModel() override = default;

  // Reads the model from the model storage.
  absl::Status Read(const ModelStorage& storage) override;

  // Provides the state reached from state following utf8_sym.
  int NextState(int state, int utf8_sym) override;

  // Copies the probs and normalization from the given state into the response.
  bool ExtractLMScores(int state, LMScores* response) override;

  // Returns the negative log probability of the utf8_sym at the state.
  double SymLMScore(int state, int utf8_sym) override;

 private:
  // Creates lexicographic ordering of symbol table for efficient summing.
  absl::Status EstablishLexicographicOrdering();

  // Finds cache index to delete.
  int FindOldestLastAccessedCache() const;

  // Returns new cache index for given state.
  absl::Status GetNewCacheIndex(fst::StdArc::StateId s,
                                const std::vector<double>& weights);

  // Returns cache index if it exists, creates new cache entry otherwise.
  absl::Status EnsureCacheIndex(int state);

  // Fills vector with cummulative costs (in lexicographic order) at the state.
  std::vector<double> FillWeightVector(int state);

  // Returns begin/end index pair for words with that symbol extension from that
  // implicit state.
  std::pair<int, int> GetBeginEndIndices(int state, int prefix_length,
                                         int utf8_sym);

  // Returns vector of end indicies of next characters and fills vector of
  // corresponding next characters.
  const std::vector<int> GetNextCharEnds(int state,
                                         std::vector<std::string>* next_chars);

  // Returns vector of end indicies of next characters and fills vector of
  // corresponding next characters, for implicit states.
  const std::vector<int> GetNextCharEnds(int state, int prefix_length,
                                         int begin_index, int end_index,
                                         std::vector<std::string>* next_chars);

  // Returns sum of probabilities over word index range from given state.
  double GetRangeCost(int state, int begin_index, int end_index);

  // Returns final cost for state from cache.
  double GetFinalCost(int state);

  // Returns final cost for state from model_,
  double GetBackedoffFinalCost(int state);

  // Returns next model state for complete word.
  int NextCompleteState(int state, int model_state, int prefix_length) const;

  // Returns next implicit state after first letter of a word.
  int NextFirstLetterState(int state, int utf8_sym);

  // Vectors for mapping between symbol table and lexicographic ordering.
  std::vector<int> lexicographic_order_;     // Lexicographic order of symbols.
  std::vector<int> lexicographic_position_;  // Lexicographic index of symbol.

  // Length of common prefix with previous symbol in lexicographic order.
  std::vector<int> previous_common_prefix_length_;

  // For establishing begin and end indices of prefixes after the first char.
  int first_char_begin_index_;  // Starting index of the lexicographic order.
  std::vector<std::string> first_chars_;  // First characters of all words.
  std::vector<int> first_char_ends_;  // Last index for each first character.

  int oov_state_;  // Implicit state for out-of-vocabulary words.

  // TODO: add locking mechanisms for updating cache and implicit states.
  // For caching word probabilities at model states for quick marginalization.
  int max_cache_size_;  // Limit on caching for garbage collection.
  int cache_accessed_;  // Counter of cache accesses to determine priority.
  std::vector<int> cache_index_;  // Index of cache for state if it exists.
  std::vector<std::unique_ptr<NGramStateCache>>
      state_cache_;  // Cache for state information.

  // Implicit state manager for the model.
  std::unique_ptr<NGramImplicitStates> ngram_implicit_states_;
};

}  // namespace models
}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_MODELS_NGRAM_WORD_FST_MODEL_H_
