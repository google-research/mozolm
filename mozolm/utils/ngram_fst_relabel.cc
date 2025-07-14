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

#include "mozolm/utils/ngram_fst_relabel.h"

#include <memory>
#include <utility>

#include "mozolm/stubs/integral_types.h"
#include "google/protobuf/stubs/logging.h"
#include "fst/arcsort.h"
#include "fst/fst.h"
#include "fst/relabel.h"
#include "fst/symbol-table.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "nisaba/port/utf8_util.h"
#include "nisaba/port/status_macros.h"

using fst::kAcceptor;
using fst::kError;
using fst::kIDeterministic;
using fst::kILabelSorted;

using fst::ArcSort;
using fst::Relabel;
using fst::RelabelSymbolTable;
using fst::StdArc;
using fst::StdILabelCompare;
using fst::StdVectorFst;
using fst::SymbolTable;

namespace mozolm {
namespace {

// The standard label for the epsilon symbol.
constexpr StdArc::Label kEpsilonLabel = 0;

// Returns a vector containing allowed (new) labels for the withheld symbols
// in a character FST.
std::vector<StdArc::Label> AllowedWithheldCharLabels() {
  std::vector<StdArc::Label> labels;
  labels.reserve(26);
  for (int i = 1; i < 9; ++i) labels.push_back(i);
  // [Shift Out, Unit Separator].
  for (int i = 14; i < 32; ++i) labels.push_back(i);
  labels.push_back(127);  // Delete.
  return labels;
}

// Builds mapping between the original labels and the Unicode codepoints of the
// characters corresponding to the original labels, excluding the symbols
// specified in `keep_symbols` list. For the kept symbols, we first check if
// they collide with the existing relabelings for the normal symbols, and if
// they do, relabel them appropriately.
//
// The relabeling mapping we construct does not include identity mappings, the
// relabeling algorithm takes care of them assuming that all the missing
// relabelings represent identity mapping.
absl::StatusOr<std::vector<std::pair<StdArc::Label, StdArc::Label>>>
GetCodepointMapping(const SymbolTable &symbols,
                    const absl::flat_hash_set<std::string> &keep_symbols) {
  std::vector<std::pair<StdArc::Label, StdArc::Label>> mapping;
  mapping.reserve(symbols.NumSymbols());
  int num_relabeled = 0, num_kept = 0;
  std::vector<StdArc::Label> kept_labels;
  kept_labels.reserve(keep_symbols.size());
  absl::flat_hash_set<StdArc::Label> new_labels;
  for (const auto &pos : symbols) {
    const std::string &symbol = pos.Symbol();
    const int64_t label = pos.Label();
    if (label == kEpsilonLabel) continue;
    if (keep_symbols.find(symbol) != keep_symbols.end()) {
      kept_labels.push_back(label);
      num_kept++;
      continue;
    }
    char32 new_label;
    if (!nisaba::utf8::DecodeSingleUnicodeChar(symbol, &new_label)) {
      return absl::UnknownError(absl::StrCat("Expected symbol \"", symbol,
                                             "\" to be a single codepoint"));
    }
    mapping.push_back({label, new_label});
    new_labels.insert(new_label);
    num_relabeled++;
  }
  // Check for collisions between the new labels and the currently kept labels.
  // Only relabel those kept symbols for which the collisions exist.
  static const std::vector<StdArc::Label> allowed_kept_relabelings =
      AllowedWithheldCharLabels();
  int num_kept_relabeled = 0;
  for (auto kept_label : kept_labels) {
    if (new_labels.find(kept_label) != new_labels.end()) {  // Collision.
      if (num_kept_relabeled >= allowed_kept_relabelings.size()) {
        return absl::UnknownError(absl::StrCat(
            "Too many kept symbol collisions. Maximum number of relabelings "
            "allowed is ", allowed_kept_relabelings.size()));
      }
      mapping.push_back({kept_label,
          allowed_kept_relabelings[num_kept_relabeled]});
      num_kept_relabeled++;
    }
  }
  GOOGLE_LOG(INFO) << num_relabeled << " symbols relabeled, " << num_kept << " kept, "
            << num_kept_relabeled << " kept symbols relabeled.";
  return mapping;
}

// Checks FST properties that transducer has to satisfy.
absl::Status CheckProperties(const StdVectorFst &fst) {
  const uint64_t need_props = kAcceptor | kIDeterministic | kILabelSorted;
  const uint64_t have_props = fst.Properties(need_props, /* test= */true);
  if (!(have_props & kAcceptor)) return absl::UnknownError("not an acceptor");
  if (!(have_props & kIDeterministic)) {
    return absl::UnknownError("not deterministic");
  }
  if (!(have_props & kILabelSorted)) {
    return absl::UnknownError("not label sorted");
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status RelabelWithCodepoints(
    const std::vector<std::string> &keep_symbols_vec, fst::StdVectorFst *fst) {
  RETURN_IF_ERROR(CheckProperties(*fst));
  GOOGLE_LOG(INFO) << "Building input/output mappings and relabeling ...";
  const absl::flat_hash_set<std::string> keep_symbols(keep_symbols_vec.begin(),
                                                      keep_symbols_vec.end());
  const SymbolTable *symbols = fst->InputSymbols();
  if (symbols == nullptr) return absl::NotFoundError("No input symbols");
  std::vector<std::pair<StdArc::Label, StdArc::Label>> mapping;
  ASSIGN_OR_RETURN(mapping, GetCodepointMapping(*symbols, keep_symbols));

  std::unique_ptr<SymbolTable> symbols_relabel(RelabelSymbolTable(
      symbols, mapping));
  fst->SetInputSymbols(symbols_relabel.get());
  fst->SetOutputSymbols(symbols_relabel.get());

  Relabel(fst, mapping, mapping);
  if (fst->Properties(kError, false)) {
    return absl::NotFoundError("Relabeling failed");
  }
  ArcSort(fst, StdILabelCompare());
  return absl::OkStatus();
}

}  // namespace mozolm
