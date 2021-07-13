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

rules_python_version = "0.3.0"

http_archive(
    name = "rules_python",
    url = "https://github.com/bazelbuild/rules_python/releases/download/%s/rules_python-%s.tar.gz" % (
        rules_python_version, rules_python_version),
    sha256 = "934c9ceb552e84577b0faf1e5a2f0450314985b4d8712b2b70717dc679fdc01b",
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

grpc_version = "1.38.1"

http_archive(
    name = "com_github_grpc_grpc",
    urls = ["https://github.com/grpc/grpc/archive/v%s.tar.gz" % grpc_version],
    sha256 = "f60e5b112913bf776a22c16a3053cc02cf55e60bf27a959fd54d7aaf8e2da6e8",
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

java_grpc_version = "1.39.0"

http_archive(
    name = "com_github_grpc_grpc_java",
    url = "https://github.com/grpc/grpc-java/archive/refs/tags/v%s.tar.gz" % java_grpc_version,
    sha256 = "85927f857e0b3ad5c4e51c2e6d29213d3e0319f20784aa2113552f71311ba74c",
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
    sha256 = "0e86f73a7b4ebeadcb62af65479c352db9e0241a05317942767ec2670e58a6fb",
    strip_prefix = "openfst-%s" % openfst_version,
)

# -------------------------------------------------------------------------
# OpenGrm N-Gram: See
#   http://www.openfst.org/twiki/bin/view/GRM/NGramLibrary
# -------------------------------------------------------------------------
opengrm_ngram_version = "1.3.13-rc1"

http_archive(
    name = "org_opengrm_ngram",
    urls = ["https://github.com/agutkin/finite_state/raw/main/ngram-%s.tar.gz" % opengrm_ngram_version],
    sha256 = "c027cee208090f35a1f725dc9cc22bc0d977adba346d765bf2e1f55990a4fa40",
    strip_prefix = "ngram-%s" % opengrm_ngram_version,
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

proto_grpc_version = "3.1.1"

http_archive(
    name = "rules_proto_grpc",
    urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/archive/refs/tags/%s.tar.gz" % (
        proto_grpc_version)],
    strip_prefix = "rules_proto_grpc-%s" % proto_grpc_version,
    sha256 = "7954abbb6898830cd10ac9714fbcacf092299fda00ed2baf781172f545120419",
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

docker_version = "0.17.0"

http_archive(
    name = "io_bazel_rules_docker",
    sha256 = "59d5b42ac315e7eadffa944e86e90c2990110a1c8075f1cd145f487e999d22b3",
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
