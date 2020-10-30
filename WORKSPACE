# Bazel (http://bazel.io/) WORKSPACE file for AAC LM Server.

workspace(name = "com_google_mozolm")

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository", "new_git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# -----
# ZLib:
# -----

http_archive(
    name = "net_zlib",
    build_file = "@com_google_protobuf//:third_party/zlib.BUILD",
    sha256 = "c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1",
    strip_prefix = "zlib-1.2.11",
    urls = ["https://zlib.net/zlib-1.2.11.tar.gz"],
)

# --------
# Protobuf
# --------

# Dependencies.
http_archive(
    name = "bazel_skylib",
    sha256 = "97e70364e9249702246c0e9444bccdc4b847bed1eb03c5a3ece4f83dfe6abc44",
    urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.2/bazel-skylib-1.0.2.tar.gz"],
)

# Protocol buffer library itself.
protobuf_version = "3.9.0"

protobuf_sha256 = "2ee9dcec820352671eb83e081295ba43f7a4157181dad549024d7070d079cf65"

# proto_library and related rules implicitly depend on @com_google_protobuf.
http_archive(
    name = "com_google_protobuf",
    sha256 = protobuf_sha256,
    strip_prefix = "protobuf-%s" % protobuf_version,
    urls = ["https://github.com/protocolbuffers/protobuf/archive/v%s.tar.gz" %
            protobuf_version],
)

# Import external protobuf dependencies into this workspace.
load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")

protobuf_deps()

# --------------------------------
# GoogleTest/GoogleMock framework.
# --------------------------------

googletest_version = "1.10.0"

http_archive(
    name = "com_google_googletest",
    urls = ["https://github.com/abseil/googletest/archive/release-%s.zip" %
            googletest_version],
    sha256 = "94c634d499558a76fa649edb13721dce6e98fb1e7018dfaeba3cd7a083945e91",
    strip_prefix = "googletest-release-%s" % googletest_version,
)

# ------------------------------------
# Google Abseil - C++ Common Libraries
# ------------------------------------

# Since at the moment the releases are very infrequent, we pull the HEAD of
# master branch. Also, "sha256" doesn't seem to work at the moment.
git_repository(
    name = "io_absl_cpp",
    commit = "ea8a689cf5e71f31f96af78859eccc11161fa36a",  # July 17, 2020.
    shallow_since = "1595021600 -0400",
    remote = "https://github.com/abseil/abseil-cpp.git",
)

# ------------------------------------
# gRPC Rules for Bazel:
# ------------------------------------
# See https://github.com/grpc/grpc/blob/master/src/cpp/README.md#make

http_archive(
    name = "com_github_grpc_grpc",
    urls = [
        "https://github.com/grpc/grpc/archive/de893acb6aef88484a427e64b96727e4926fdcfd.tar.gz",
    ],
    sha256 = "61272ea6d541f60bdc3752ddef9fd4ca87ff5ab18dd21afc30270faad90c8a34",
    strip_prefix = "grpc-de893acb6aef88484a427e64b96727e4926fdcfd",
)
load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")
grpc_deps()

# Not mentioned in official docs... mentioned here https://github.com/grpc/grpc/issues/20511
load("@com_github_grpc_grpc//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")
grpc_extra_deps()

# Local Variables:
# mode: python
# End:
