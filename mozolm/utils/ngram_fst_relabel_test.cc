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

// Tests for n-gram FST relabeling APIs.

#include "mozolm/utils/ngram_fst_relabel.h"

#include <memory>
#include <set>
#include <sstream>

#include "fst/fst.h"
#include "fst/symbol-table.h"
#include "fst/vector-fst.h"
#include "fst/script/compile-impl.h"
#include "gmock/gmock.h"
#include "nisaba/port/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"

using fst::ArcIterator;
using fst::FstCompiler;
using fst::StateIterator;
using fst::StdArc;
using fst::StdVectorFst;
using fst::SymbolTable;
using fst::kError;

namespace mozolm {
namespace {

// Checks that the symbol's label is its corresponding ASCII encoding.
void CheckSymbolCodepoint(const absl::string_view symbol, StdArc::Label label) {
  EXPECT_FALSE(symbol.empty()) << "Label " << label << " not found";
  ASSERT_EQ(1, symbol.size());
  EXPECT_EQ(label, static_cast<StdArc::Label>(symbol[0]));
}

// Checks that the `fst` has been relabeled properly with all the labels (modulo
// the withheld symbols in `keep_labels`) corresponding to Unicode codepoints.
void CheckRelabledCharFst(const StdVectorFst &fst,
                          const std::set<StdArc::Label> &keep_labels) {
  // Check symbol table.
  const SymbolTable &syms = *fst.InputSymbols();
  for (const auto &pos : syms) {
    const StdArc::Label &label = pos.Label();
    if (label == 0) continue;  // Epsilon.
    if (keep_labels.find(label) != keep_labels.end()) continue;
    CheckSymbolCodepoint(pos.Symbol(), label);
  }

  // Check the automaton.
  for (StateIterator<StdVectorFst> siter(fst); !siter.Done(); siter.Next()) {
    for (ArcIterator<StdVectorFst> aiter(fst, siter.Value()); !aiter.Done();
         aiter.Next()) {
      const StdArc &arc = aiter.Value();
      EXPECT_EQ(arc.ilabel, arc.olabel);
      if (keep_labels.find(arc.ilabel) != keep_labels.end()) continue;
      const std::string &symbol = syms.Find(arc.ilabel);
      CheckSymbolCodepoint(symbol, arc.ilabel);
    }
  }
}

// Builds FST given the textual representation of its symbol table and contents.
StdVectorFst BuildFst(const std::string &text_symbols,
                      const std::string &text_contents) {
  std::istringstream sym_file(text_symbols);
  std::unique_ptr<SymbolTable> syms(SymbolTable::ReadText(sym_file, "unused"));
  std::istringstream fst_file(text_contents);
  const FstCompiler<StdArc> compiler(
      fst_file, /* source= */"", syms.get(), syms.get(),
      /* ssyms= */nullptr, /* accep= */false, /* ikeep= */true,
      /* okeep= */true, /* nkeep= */false);
  return compiler.Fst();
}

// Checks relabeling of character n-gram FSTs.
TEST(NGramFstRelabelTest, CodepointRelabeling) {
  // Checks empty FST.
  StdVectorFst empty_fst;
  EXPECT_FALSE(RelabelWithCodepoints({}, &empty_fst).ok());

  // Check a simple scenario with four symbols, one of which is added to the
  // withheld set and there is no collision between the kept symbol and the
  // existing symbols.
  StdVectorFst fst = BuildFst(
      // Symbols.
      "<epsilon> 0\n"
      "a 1\n"
      "b 2\n"
      "c 3\n"
      "keep 4\n",
      // Topology.
      "1 2 a a\n"
      "2 3 b b\n"
      "3 4 c c\n"
      "5 6 keep keep\n"
      "4\n");
  ASSERT_FALSE(fst.Properties(kError, false));

  // First try to relabel with an empty `keep` set. This should fail because of
  // the presence of the `keep` symbol.
  EXPECT_FALSE(RelabelWithCodepoints({}, &fst).ok());
  EXPECT_OK(RelabelWithCodepoints({"keep"}, &fst));
  CheckRelabledCharFst(fst, {4});

  // Same as above, with the kept symbols colliding. The colliding `keep` symbol
  // should be relabeled to the first available free label, i.e. 1.
  StdVectorFst naughty_fst = BuildFst(
      // Symbols.
      "<epsilon> 0\n"
      "a 1\n"
      "b 2\n"
      "c 3\n"
      "keep 98\n",  // `keep` collides with `b` after relabeling.
      // Topology.
      "1 2 a a\n"
      "2 3 b b\n"
      "3 4 c c\n"
      "5 6 keep keep\n"
      "4\n");
  ASSERT_FALSE(naughty_fst.Properties(kError, false));
  EXPECT_FALSE(RelabelWithCodepoints({}, &naughty_fst).ok());
  EXPECT_OK(RelabelWithCodepoints({"keep"}, &naughty_fst));
  CheckRelabledCharFst(naughty_fst, {1});
}

}  // namespace
}  // namespace mozolm
