# Copyright 2021 MozoLM Authors.
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

"""Starlark alias-like rules for compiling protocol buffers and gRPC services."""

load(
    "@rules_proto_grpc//js:defs.bzl",
    js_grpc_node_library_impl = "js_grpc_node_library",
    js_grpc_web_library_impl = "js_grpc_web_library",
    js_proto_library_impl = "js_proto_library",
)

def jspb_proto_library(
        name,
        deps = None,
        **kwds):
    """Compiles protocol buffer into Javascript code."""
    js_proto_library_impl(
        name,
        # We assume that the `deps` includes pure protocol buffer targets only.
        protos = deps,
        **kwds
    )

def js_grpc_web_library(
        name,
        src,
        deps = None,
        **kwds):
    """Compile protocol buffer and gRPC stubs into Javascript code.

    Args:
       name: Rile name.
       src: Main protocol buffer source file (.proto).
       deps: Dependencies. These should be compiled `proto_library` artifacts.
    """
    if not src.endswith(".proto"):
        fail("Expecting a single protocol buffer source file for `src`. " +
             "Got %s" % src)
    if src.startswith(":") or src.startswith("//"):
        fail("Argument `src` should have a file rather than a rule")
    proto_rule_for_src = ":" + src.replace(".", "_")

    js_grpc_web_library_impl(
        name + "_web",
        protos = [proto_rule_for_src],
        deps = deps,
        **kwds
    )
    js_grpc_node_library_impl(
        name + "_node",
        protos = [proto_rule_for_src],
        deps = deps,
        **kwds
    )
