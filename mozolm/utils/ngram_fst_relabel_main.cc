// Copyright 2023 MozoLM Authors.
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

// UTF8-encoded string symbols to Unicode codepoint relabeling in char FSTs.
//
// The transducer input and output symbols should consist of single UTF8-encoded
// Unicode characters apart from the symbols explicitly specified using
// `keep_symbols` command-line flag.
//
// Example:
// --------
//   FST_MODEL_DIR=bazel-bin/third_party/mozolm/external/models/mtu
//   bazel build -c opt mozolm/utils:ngram_fst_relabel
//   bazel-bin/mozolm/utils/ngram_fst_relabel \
//     --input_fst_file ${FST_MODEL_DIR}/dasher_eng_4gram_arpa.fst \
//     --keep_symbols "<sp>" \
//     --output_fst_file /tmp/relabeled.fst

#include <memory>
#include <string>
#include <vector>

#include "google/protobuf/stubs/logging.h"
#include "fst/vector-fst.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mozolm/utils/ngram_fst_relabel.h"
#include "nisaba/port/status_macros.h"

ABSL_FLAG(std::string, input_fst_file, "",
          "Input n-gram character model file in FST format.");

ABSL_FLAG(std::string, output_fst_file, "",
          "Output n-gram character model file in FST format.");

ABSL_FLAG(std::vector<std::string>, keep_symbols, {},
          "List of symbols in the symbol table that are not relabeled.");

using fst::StdVectorFst;

namespace mozolm {
namespace {

// Reads FST, performs relabeling operations on it, and saves the resulting
// transducer.
absl::Status RelabelAndSave(const std::string &input_file,
                            const std::string &output_file) {
  // Load.
  GOOGLE_LOG(INFO) << "Reading FST from " << input_file << " ...";
  std::unique_ptr<fst::StdVectorFst> fst;
  fst.reset(StdVectorFst::Read(input_file));
  if (!fst) {
    return absl::NotFoundError(absl::StrCat("Failed to read FST from ",
                                            input_file));
  }
  // Assuming the model symbols are UTF8-encoded characters, relabels the symbol
  // tables to use the corresponding Unicode codepoints as the labels.
  const std::vector<std::string> &keep_symbols = absl::GetFlag(
      FLAGS_keep_symbols);
  RETURN_IF_ERROR(RelabelWithCodepoints(keep_symbols, fst.get()));

  // Save.
  GOOGLE_LOG(INFO) << "Saving relabeled FST to " << output_file << " ...";
  if (!fst->Write(output_file)) {
    return absl::UnknownError(absl::StrCat("Failed to save to ", output_file));
  }
  return absl::OkStatus();
}

}  // namespace
}  // namespace mozolm

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  const std::string &input_fst_file = absl::GetFlag(FLAGS_input_fst_file);
  if (input_fst_file.empty()) {
    GOOGLE_LOG(ERROR) << "Input FST file not specified!";
    return 1;
  }
  const std::string &output_fst_file = absl::GetFlag(FLAGS_output_fst_file);
  if (output_fst_file.empty()) {
    GOOGLE_LOG(ERROR) << "Output FST file not specified!";
    return 1;
  }
  const auto status = mozolm::RelabelAndSave(input_fst_file, output_fst_file);
  if (!status.ok()) {
    GOOGLE_LOG(ERROR) << "Relabeling failed: " << status.ToString();
    return 1;
  }
  return 0;
}
