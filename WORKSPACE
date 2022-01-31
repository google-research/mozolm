# Bazel (http://bazel.io/) workspace file for MozoLM server.

workspace(name = "com_google_mozolm")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# ------------------------------------
# Bazel rules:
# ------------------------------------
# See https://github.com/bazelbuild/rules_python.
#
# This entry is a bit of a kludge. It makes sure that whichever repos that
# import `rules_python` import the latest official release. Without it, it
# seems to be impossible to import `io_bazel_rules_docker` below successfully.

rules_python_version = "0.6.0"

http_archive(
    name = "rules_python",
    url = "https://github.com/bazelbuild/rules_python/archive/%s.tar.gz" % (
        rules_python_version),
    strip_prefix = "rules_python-%s" % rules_python_version,
    sha256 = "a30abdfc7126d497a7698c29c46ea9901c6392d6ed315171a6df5ce433aa4502",
)

# ----------------------------------------------
# Nisaba: Script processing library from Google:
# ----------------------------------------------
# We depend on some of core C++ libraries from Nisaba and use the fresh code
# from the HEAD. See
#   https://github.com/google-research/nisaba

nisaba_version = "main"

http_archive(
    name = "com_google_nisaba",
    url = "https://github.com/google-research/nisaba/archive/refs/heads/%s.zip" % nisaba_version,
    strip_prefix = "nisaba-%s" % nisaba_version,
)

load("@com_google_nisaba//bazel:workspace.bzl", "nisaba_public_repositories")

nisaba_public_repositories()

# ------------------------------------
# gRPC (C++) package for Bazel:
# ------------------------------------
# See https://github.com/grpc/grpc/blob/master/src/cpp/README.md#make

grpc_version = "1.43.0"

http_archive(
    name = "com_github_grpc_grpc",
    urls = ["https://github.com/grpc/grpc/archive/v%s.tar.gz" % grpc_version],
    sha256 = "9647220c699cea4dafa92ec0917c25c7812be51a18143af047e20f3fb05adddc",
    strip_prefix = "grpc-%s" % grpc_version,
)

# --------------------------------------------------------
# External Java rules and Maven (required for gRPC Java):
# --------------------------------------------------------
# See https://github.com/bazelbuild/rules_jvm_external

rules_jvm_version = "4.1"

http_archive(
    name = "rules_jvm_external",
    url = "https://github.com/bazelbuild/rules_jvm_external/archive/refs/tags/%s.tar.gz" % rules_jvm_version,
    strip_prefix = "rules_jvm_external-%s" % rules_jvm_version,
    sha256 = "995ea6b5f41e14e1a17088b727dcff342b2c6534104e73d6f06f1ae0422c2308",
)

load("@rules_jvm_external//:defs.bzl", "maven_install")

# ------------------------------------
# gRPC (Java) package for Bazel:
# ------------------------------------
# See https://github.com/grpc/grpc-java

java_grpc_version = "1.43.2"

http_archive(
    name = "com_github_grpc_grpc_java",
    url = "https://github.com/grpc/grpc-java/archive/refs/tags/v%s.tar.gz" % java_grpc_version,
    sha256 = "6c39c5feecda4f1ccafe88d8928d9a0f2a686d9a9a9c03888a2e5ac92f7ee34a",
    strip_prefix = "grpc-java-%s" % java_grpc_version,
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

# -------------------------------------------------------------------------
# OpenFst: See
#   http://www.openfst.org/twiki/pub/FST/FstDownload/README
# -------------------------------------------------------------------------
openfst_version = "1.8.2-rc1"

http_archive(
    name = "org_openfst",
    urls = ["https://github.com/agutkin/finite_state/raw/main/openfst-%s.tar.gz" % openfst_version],
    sha256 = "2b655782033257c7e51e6057a005e1000af858a98a8ef06502f74d9fc55ca276",
    strip_prefix = "openfst-%s" % openfst_version,
)

# -------------------------------------------------------------------------
# Protocol buffer matches (should be part of gmock and gtest, but not yet):
#   https://github.com/inazarenko/protobuf-matchers
# -------------------------------------------------------------------------
# TODO: Get rid of `protobuf-matchers` once the maintainer of gtest
# and gmock introduce it in their repos. See the discussion here:
#   https://github.com/google/googletest/issues/1761

http_archive(
    name = "com_github_protobuf_matchers",
    urls = ["https://github.com/inazarenko/protobuf-matchers/archive/refs/heads/master.zip"],
    strip_prefix = "protobuf-matchers-master",
)

# -------------------------------------------------------------------------
# Android cross-compile configuration:
#   https://github.com/tensorflow/tensorflow/tree/master/third_party/android
# -------------------------------------------------------------------------

load("//third_party/android:android_configure.bzl", "android_configure")
android_configure(name = "local_config_android")

load("@local_config_android//:android.bzl", "android_workspace")
android_workspace()

# ------------------------------------------------------------------------------
# Java version of gRPC: Miscellaneous required dependencies for supporting
# the `java_grpc_libray` rule. See:
#   https://github.com/grpc/grpc-java/blob/master/examples/WORKSPACE
# ------------------------------------------------------------------------------

load("@com_github_grpc_grpc_java//:repositories.bzl", "IO_GRPC_GRPC_JAVA_ARTIFACTS")
load("@com_github_grpc_grpc_java//:repositories.bzl", "IO_GRPC_GRPC_JAVA_OVERRIDE_TARGETS")

maven_install(
    artifacts = IO_GRPC_GRPC_JAVA_ARTIFACTS,
    generate_compat_repositories = True,
    override_targets = IO_GRPC_GRPC_JAVA_OVERRIDE_TARGETS,
    repositories = ["https://repo.maven.apache.org/maven2/"],
)

load("@maven//:compat.bzl", "compat_repositories")
compat_repositories()

load("@com_github_grpc_grpc_java//:repositories.bzl", "grpc_java_repositories")
grpc_java_repositories()

# ------------------------------------
# Pure JavaScript for Bazel & gRPC:
# ------------------------------------
# See:
#   https://github.com/rules-proto-grpc/rules_proto_grpc
#   https://rules-proto-grpc.aliddell.com/en/latest/lang/js.html

proto_grpc_version = "4.1.1"

http_archive(
    name = "rules_proto_grpc",
    urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/archive/refs/tags/%s.tar.gz" % (
        proto_grpc_version)],
    strip_prefix = "rules_proto_grpc-%s" % proto_grpc_version,
    sha256 = "507e38c8d95c7efa4f3b1c0595a8e8f139c885cb41a76cab7e20e4e67ae87731",
)

load("@rules_proto_grpc//:repositories.bzl", "rules_proto_grpc_toolchains")

rules_proto_grpc_toolchains()

load("@rules_proto_grpc//js:repositories.bzl", rules_proto_grpc_js_repos = "js_repos")

rules_proto_grpc_js_repos()

load("@build_bazel_rules_nodejs//:index.bzl", "yarn_install")

yarn_install(
    name = "npm",
    # This should be changed to your local package.json which should contain the
    # dependencies required.
    package_json = "@rules_proto_grpc//js:requirements/package.json",
    yarn_lock = "@rules_proto_grpc//js:requirements/yarn.lock",
)

# -------------------------------------------------------------------------
# Bazel container image rules:
#   https://github.com/bazelbuild/rules_docker
# -------------------------------------------------------------------------

docker_version = "0.23.0"

http_archive(
    name = "io_bazel_rules_docker",
    sha256 = "85ffff62a4c22a74dbd98d05da6cf40f497344b3dbf1e1ab0a37ab2a1a6ca014",
    strip_prefix = "rules_docker-%s" % docker_version,
    urls = ["https://github.com/bazelbuild/rules_docker/releases/download/v%s/rules_docker-v%s.tar.gz" % (
        docker_version, docker_version)],
)

load("@io_bazel_rules_docker//repositories:repositories.bzl", container_repositories = "repositories")

container_repositories()

load("@io_bazel_rules_docker//repositories:deps.bzl", container_deps = "deps")

container_deps()

load("@io_bazel_rules_docker//container:pull.bzl", "container_pull")

# Pull the base Linux distribution: Should be between five and six megabytes.
container_pull(
    name = "alpine_linux_amd64",
    registry = "index.docker.io",
    repository = "library/alpine",
    tag = "3.13.5",
    digest = "sha256:def822f9851ca422481ec6fee59a9966f12b351c62ccb9aca841526ffaa9f748",
)

load("@io_bazel_rules_docker//cc:image.bzl", _cc_image_repos = "repositories")

_cc_image_repos()

# Local Variables:
# mode: bazel-build
# End:
