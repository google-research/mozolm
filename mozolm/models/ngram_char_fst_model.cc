// Copyright 2024 MozoLM Authors.
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

#include "google/protobuf/stubs/logging.h"
#include "fst/fst.h"
#include "fst/matcher.h"
#include "absl/memory/memory.h"
#include "nisaba/port/utf8_util.h"

using fst::MATCH_INPUT;
using fst::Matcher;
using fst::StdArc;
using fst::StdVectorFst;
using fst::Times;

namespace mozolm {
namespace models {

fst::StdArc::Label NGramCharFstModel::SymLabel(int utf8_sym) const {
  if (utf8_sym == 0) return utf8_sym;
  const std::string u_char = nisaba::utf8::EncodeUnicodeChar(utf8_sym);
  fst::StdArc::Label label = fst_->InputSymbols()->Find(u_char);
  if (label == fst::kNoSymbol) {
    label = oov_label_;
  }
  return label;
}

int NGramCharFstModel::NextState(int state, int utf8_sym) {
  // Perform sanity check on the incoming unicode label.
  return NextModelState(CheckCurrentState(state), SymLabel(utf8_sym));
}

bool NGramCharFstModel::ExtractLMScores(int state, LMScores *response) {
  const StdArc::StateId current_state = CheckCurrentState(state);

  // Compute the label probability distribution for the given state.
  // TODO: may be faster to collect all symbols simultaneously.
  const auto &symbols = *fst_->InputSymbols();
  const int num_symbols = symbols.NumSymbols();
  response->mutable_symbols()->Reserve(num_symbols);
  response->mutable_probabilities()->Reserve(num_symbols);
  std::vector<double> costs;
  costs.reserve(num_symbols);
  costs.push_back(LabelCostInState(current_state, 0).Value());
  response->add_symbols("");  // End-of-string is empty string by convention.
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

double NGramCharFstModel::SymLMScore(int state, int utf8_sym) {
  return LabelCostInState(state, SymLabel(utf8_sym)).Value();
}

StdArc::Weight NGramCharFstModel::LabelCostInState(StdArc::StateId state,
                                                   StdArc::Label label) const {
  // End-of-string is label 0 by convention.
  if (label == 0) return FinalCostInState(state);
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

StdArc::Weight NGramCharFstModel::FinalCostInState(
    StdArc::StateId state) const {
  StdArc::StateId current_state = state;
  StdArc::Weight cost = fst_->Final(current_state);
  StdArc::Weight bo_cost = StdArc::Weight::One();
  while (current_state >= 0 && cost == StdArc::Weight::Zero()) {
    StdArc::Weight this_bo_cost;
    current_state = model_->GetBackoff(current_state, &this_bo_cost);
    bo_cost = Times(bo_cost, this_bo_cost);
    cost = fst_->Final(current_state);
    if (cost != StdArc::Weight::Zero()) {
      cost = Times(cost, bo_cost);
    }
  }
  return cost;
}

}  // namespace models
}  // namespace mozolm
