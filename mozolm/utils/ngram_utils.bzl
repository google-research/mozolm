# Copyright 2022 MozoLM Authors.
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

"""OpenGrm n-gram miscellaneous build utilities."""

def ngram_convert_arpa_to_fst(
        name,
        relabel_to_codepoints = False,
        relabel_keep_symbols = None,
        deps = None,
        out = None,
        **kwds):
    """Converts compressed n-gram language model in ARPA format to FST.

    Assumes that the input file name is `name`.arpa.gz, where `name` is the name
    of this rule. The output file name will be `out`.fst (if `out` is specified)
    or `name`.fst otherwise.

    Args:
      name: The BUILD rule name and the file prefix for the generated output.
      relabel_to_codepoints: If enabled, relabel the FST symbols to their
        corresponding Unicode codepoints.
      relabel_keep_symbols: When relabeling, do not relabel symbols in this
        list.
      deps: A list of other grammar targets that we'll need for this grammar.
      out: FST file to be generated. If not present, then we'll use the `name`
           followed by ".fst".
      **kwds: Attributes common to all BUILD rules, e.g., testonly, visibility.
    """
    output_name = out if out else name
    uncompressed_arpa_file = output_name + ".arpa"
    native.genrule(
        name = name + "_uncompress",
        srcs = [name + ".arpa.gz"],
        outs = [uncompressed_arpa_file],
        cmd = "gunzip -c $< > $@",
    )

    # Convert from ARPA to FST format.
    converter_rule = "@org_opengrm_ngram//:ngramread"
    original_output_name = output_name
    if relabel_to_codepoints:
        # This is going to be an intermediate target.
        output_name += "_tmp"
    native.genrule(
        name = output_name + "_fst",
        srcs = [":%s_uncompress" % name],
        outs = [output_name + ".fst"],
        cmd = "$(location @org_opengrm_ngram//:ngramread) --ARPA $< $@",
        tools = [
            converter_rule,
        ],
    )

    # Relabel, if requested.
    if not relabel_to_codepoints:
        return
    relabeler_rule = "//mozolm/utils:ngram_fst_relabel"
    keep_symbols = "--keep_symbols \"%s\"" % ",".join(relabel_keep_symbols) if relabel_keep_symbols else ""
    native.genrule(
        name = original_output_name + "_fst",
        srcs = [":%s.fst" % output_name],
        outs = [original_output_name + ".fst"],
        cmd = """
        $(location %s) --input_fst_file $< --output_fst_file $@ %s
        """ % (relabeler_rule, keep_symbols),
        tools = [
            relabeler_rule,
        ],
    )
