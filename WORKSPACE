# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Bazel (http://bazel.io/) workspace file for MozoLM server.

workspace(name = "com_google_mozolm")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# ------------------------------------
# gRPC package for Bazel:
# ------------------------------------
# See https://github.com/grpc/grpc/blob/master/src/cpp/README.md#make

grpc_version = "1.33.2"

http_archive(
    name = "com_github_grpc_grpc",
    urls = ["https://github.com/grpc/grpc/archive/v%s.tar.gz" % grpc_version],
    sha256 = "2060769f2d4b0d3535ba594b2ab614d7f68a492f786ab94b4318788d45e3278a",
    strip_prefix = "grpc-%s" % grpc_version,
)

# ------------------------------------
# gRPC dependencies:
# ------------------------------------
# Check the list of third-party dependencies accessible through gRPC here:
#
#    https://github.com/grpc/grpc/tree/master/third_party
#
# Please do not specify these dependencies separately as they are likely to
# clash with the versions provided by gRPC.

load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")
grpc_deps()

# Not mentioned in official docs. Mentioned here: https://github.com/grpc/grpc/issues/20511
load("@com_github_grpc_grpc//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")
grpc_extra_deps()

# Local Variables:
# mode: bazel-build-mode
# End:
