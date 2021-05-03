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

#include "mozolm/models/ngram_char_fst_model.h"

#include <cmath>

#include "mozolm/stubs/logging.h"
#include "fst/fst.h"
#include "fst/matcher.h"
#include "fst/symbol-table.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "mozolm/utils/utf8_util.h"

using fst::MATCH_INPUT;
using fst::Matcher;
using fst::StdArc;
using fst::StdVectorFst;
using fst::SymbolTable;
using fst::Times;

namespace mozolm {
namespace models {
namespace {

// Label that maps to unknown symbols.
const char kUnknownSymbol[] = "<unk>";

}  // namespace

absl::Status NGramCharFstModel::Read(const ModelStorage &storage) {
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
  model_ = absl::make_unique<const ngram::NGramModel<StdArc>>(*fst_);
  return CheckModel();
}

int NGramCharFstModel::NextState(int state, int utf8_sym) {
  // Perform sanity check on the incoming unicode label.
  int64_t label = utf8_sym;
  const std::string u_char = utf8::EncodeUnicodeChar(utf8_sym);
  if (fst_->InputSymbols()->Find(u_char) == fst::kNoSymbol) {
    label = oov_label_;
  }

  StdArc::Weight bo_weight;
  StdArc::StateId current_state = CheckCurrentState(state);
  while (true) {
    Matcher<StdVectorFst> matcher(*fst_, MATCH_INPUT);
    matcher.SetState(current_state);
    if (matcher.Find(label)) {  // Arc found out of current state.
      const StdArc arc = matcher.Value();
      return arc.nextstate;
    } else {
      current_state = model_->GetBackoff(current_state, &bo_weight);
      if (current_state < 0) return current_state;
    }
  }
  // TODO: Not entirely sure if this should be -1 instead.
  return current_state;
}

bool NGramCharFstModel::ExtractLMScores(int state, LMScores *response) {
  const StdArc::StateId current_state = CheckCurrentState(state);

  // Compute the label probability distribution for the given state.
  const auto &symbols = *fst_->InputSymbols();
  const int num_symbols = symbols.NumSymbols();
  response->mutable_symbols()->Reserve(num_symbols - 1);
  response->mutable_probabilities()->Reserve(num_symbols - 1);
  std::vector<double> costs;
  costs.reserve(num_symbols - 1);
  for (int i = 1; i < num_symbols; ++i) {  // Ignore epsilon.
    const StdArc::Label label = symbols.GetNthKey(i);
    const StdArc::Weight cost = LabelCostInState(current_state, label);
    costs.push_back(cost.Value());
    response->add_symbols(symbols.Find(label));
  }
  SoftmaxRenormalize(&costs);
  for (auto cost : costs) {
    response->add_probabilities(std::exp(-cost));
  }
  response->set_normalization(1.0);
  return true;
}

StdArc::StateId NGramCharFstModel::CheckCurrentState(
    StdArc::StateId state) const {
  StdArc::StateId current_state = state;
  if (state < 0) {
    current_state = model_->UnigramState();
    if (current_state < 0) current_state = fst_->Start();
  }
  return current_state;
}

StdArc::Weight NGramCharFstModel::LabelCostInState(StdArc::StateId state,
                                                   StdArc::Label label) const {
  StdArc::Weight cost = StdArc::Weight::One();
  StdArc::StateId current_state = state;
  Matcher<StdVectorFst> matcher(*fst_, MATCH_INPUT);
  while (current_state >= 0) {
    matcher.SetState(current_state);
    if (matcher.Find(label)) {
      const StdArc arc = matcher.Value();
      cost = Times(cost, arc.weight);
      return cost;
    } else {
      StdArc::Weight bo_cost;
      current_state = model_->GetBackoff(current_state, &bo_cost);
      cost = Times(cost, bo_cost);
    }
  }
  return StdArc::Weight::Zero();
}

bool NGramCharFstModel::UpdateLMCounts(int32 state,
                                       const std::vector<int> &utf8_syms,
                                       int64 count) {
  // Updating counts on read-only model is not supported.
  return true;  // Treat as a no-op.
}

absl::Status NGramCharFstModel::CheckModel() const {
  if (model_->Error()) {
    return absl::InternalError("Model initialization failed");
  } else if (!model_->CheckTopology()) {
    return absl::InternalError(
        "FST topology does not correspond to a valid language model");
  } else if (!model_->CheckNormalization()) {
    return absl::InternalError("FST states are not fully normalized");
  }
  return absl::OkStatus();
}

}  // namespace models
}  // namespace mozolm
