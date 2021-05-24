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
#include "absl/memory/memory.h"
#include "mozolm/utils/utf8_util.h"

using fst::MATCH_INPUT;
using fst::Matcher;
using fst::StdArc;
using fst::StdVectorFst;
using fst::Times;

namespace mozolm {
namespace models {

int NGramCharFstModel::NextState(int state, int utf8_sym) {
  // Perform sanity check on the incoming unicode label.
  int64_t label = utf8_sym;
  const std::string u_char = utf8::EncodeUnicodeChar(utf8_sym);
  if (fst_->InputSymbols()->Find(u_char) == fst::kNoSymbol) {
    label = oov_label_;
  }
  return NextModelState(CheckCurrentState(state), label);
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

}  // namespace models
}  // namespace mozolm
