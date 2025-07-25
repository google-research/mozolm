# Copyright 2025 MozoLM Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Package that wrap the models from Keith Vertanen group at Michigan Tech.
#
# See the README.md file in this directory.

load("//mozolm/utils:ngram_utils.bzl", "ngram_convert_arpa_to_fst")

package(
    default_visibility = ["//mozolm:__subpackages__"],
)

licenses(["notice"])

exports_files([
    "lm_feb21_dasher_5k_tiny_3gram.arpa.gz",
    "lm_feb21_dasher_char_4gram.arpa.gz",
])

# ------------------------------------------------------------------------------
# Character language models for English.
# ------------------------------------------------------------------------------

# Following rule will create `dasher_feb21_eng_char_4gram.fst` out of
# `lm_feb21_dasher_char_4gram.arpa.gz`.
ngram_convert_arpa_to_fst(
    name = "lm_feb21_dasher_char_4gram",
    out = "dasher_feb21_eng_char_4gram",
    relabel_keep_symbols = ["<sp>"],
    relabel_to_codepoints = True,
)

# ------------------------------------------------------------------------------
# Word-based language models for English.
# ------------------------------------------------------------------------------

ngram_convert_arpa_to_fst(
    name = "lm_feb21_dasher_5k_tiny_3gram",
    out = "dasher_feb21_eng_word_5k_3gram",
)

# ------------------------------------------------------------------------------
# All converted FSTs.
# ------------------------------------------------------------------------------
_CONVERTED_FSTS = [
    ":dasher_feb21_eng_char_4gram.fst",
    ":dasher_feb21_eng_word_5k_3gram.fst",
]

filegroup(
    name = "fst_models",
    srcs = select({
       "@bazel_tools//src/conditions:windows": [],
       "//conditions:default": _CONVERTED_FSTS,
    }),
)
