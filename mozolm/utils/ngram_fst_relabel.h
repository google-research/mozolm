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

// N-gram LM FST relabeling utilities.

#ifndef MOZOLM_MOZOLM_UTILS_NGRAM_FST_RELABEL_H_
#define MOZOLM_MOZOLM_UTILS_NGRAM_FST_RELABEL_H_

#include <string>
#include <vector>

#include "fst/vector-fst.h"
#include "absl/status/status.h"

namespace mozolm {

// Assuming the model symbols are UTF8-encoded characters, relabels the symbol
// tables to use the corresponding Unicode codepoints as the labels. The list
// of symbols to ignore when relabeling is given by `keep_symbols`.
absl::Status RelabelWithCodepoints(const std::vector<std::string> &keep_symbols,
                                   fst::StdVectorFst *fst);

}  // namespace mozolm

#endif  // MOZOLM_MOZOLM_UTILS_NGRAM_FST_RELABEL_H_
