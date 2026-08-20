// Copyright 2026 MozoLM Authors.
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

#include "mozolm/models/ngram_fst_model.h"

#include <memory>

#include "google/protobuf/stubs/logging.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "fst/fst.h"
#include "fst/matcher.h"
#include "fst/symbol-table.h"
#include "third_party/opengrm/sfst/backoff.h"
#include "third_party/opengrm/sfst/canonical.h"
#include "third_party/opengrm/sfst/normalize.h"

using fst::MATCH_INPUT;
using fst::Matcher;
using fst::StdArc;
using fst::StdVectorFst;
using fst::SymbolTable;

namespace mozolm {
namespace models {
namespace {

// Label that maps to unknown symbols.
const char kUnknownSymbol[] = "<unk>";

// Failure/backoff transition label (phi arc label).
constexpr fst::StdArc::Label kPhiLabel = 0;

}  // namespace

absl::Status NGramFstModel::Read(const ModelStorage &storage) {
  if (storage.model_file().empty()) {
    return absl::InvalidArgumentError("Model file not specified");
  }
  GOOGLE_LOG(INFO) << "Initializing from " << storage.model_file() << " ...";
  std::unique_ptr<fst::StdVectorFst> fst;
  fst.reset(StdVectorFst::Read(storage.model_file()));
  if (!fst) {
    return absl::NotFoundError(absl::StrCat("Failed to read FST from ",
                                            storage.model_file()));
  }
  const SymbolTable *input_symbols = fst->InputSymbols();
  if (input_symbols == nullptr) {
    if (storage.vocabulary_file().empty()) {
      return absl::NotFoundError("FST is missing an input symbol table");
    }
    // Read symbol table from configuration.
    input_symbols = SymbolTable::Read(storage.vocabulary_file());
    if (input_symbols == nullptr) {
      return absl::NotFoundError(absl::StrCat("Failed to read symbols from ",
                                              storage.vocabulary_file()));
    }
    fst->SetInputSymbols(input_symbols);
  }
  oov_label_ = input_symbols->Find(kUnknownSymbol);
  fst_ = std::move(fst);
  sfst::Backoff<StdArc> backoff(*fst_, kPhiLabel,
                                /*require_backoff_complete=*/false);
  hi_order_ = backoff.MaxOrder();
  unigram_state_ = backoff.GetBackoffState(fst_->Start());
  if (unigram_state_ == fst::kNoStateId) {
    unigram_state_ = fst_->Start();
  }
  return CheckModel();
}

bool NGramFstModel::UpdateLMCounts(int32_t state,
                                   const std::vector<int> &utf8_syms,
                                   int64_t count) {
  // Updating counts on read-only model is not supported.
  return true;  // Treat as a no-op.
}

StdArc::StateId NGramFstModel::CheckCurrentState(
    StdArc::StateId state) const {
  StdArc::StateId current_state = state;
  if (state == fst::kNoStateId) {
    current_state = unigram_state_;
    if (current_state == fst::kNoStateId) current_state = fst_->Start();
  }
  return current_state;
}

absl::Status NGramFstModel::CheckModel() const {
  if (!sfst::IsCanonical(*fst_, kPhiLabel)) {
    return absl::InternalError(
        "FST topology does not correspond to a valid language model");
  } else if (!sfst::IsNormalized(*fst_, kPhiLabel)) {
    return absl::InternalError("FST states are not fully normalized");
  }
  return absl::OkStatus();
}

StdArc::StateId NGramFstModel::GetBackoff(StdArc::StateId state,
                                          StdArc::Weight* bo_cost) const {
  if (state == fst::kNoStateId) {
    if (bo_cost != nullptr) *bo_cost = StdArc::Weight::One();
    return fst::kNoStateId;
  }
  Matcher<StdVectorFst> matcher(*fst_, MATCH_INPUT);
  matcher.SetState(state);
  if (matcher.Find(kPhiLabel)) {
    for (; !matcher.Done(); matcher.Next()) {
      const StdArc arc = matcher.Value();
      if (arc.ilabel == fst::kNoLabel || arc.nextstate == state) continue;
      if (bo_cost != nullptr) *bo_cost = arc.weight;
      return arc.nextstate;
    }
  }
  if (bo_cost != nullptr) *bo_cost = StdArc::Weight::One();
  return fst::kNoStateId;
}

StdArc::StateId NGramFstModel::NextModelState(StdArc::StateId current_state,
                                              StdArc::Label label) const {
  StdArc::StateId return_state = unigram_state_;  // Default.
  Matcher<StdVectorFst> matcher(*fst_, MATCH_INPUT);
  while (current_state != fst::kNoStateId) {
    matcher.SetState(current_state);
    if (matcher.Find(label)) {  // Arc found out of current state.
      const StdArc arc = matcher.Value();
      return_state = arc.nextstate;
      break;
    } else {
      current_state = GetBackoff(current_state, /*bo_cost=*/nullptr);
    }
  }
  return return_state;
}

}  // namespace models
}  // namespace mozolm
