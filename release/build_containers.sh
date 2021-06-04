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

#!/bin/bash
#
# Utility script for building container images.
#
# This script needs to run on Unix (Linux or macOS) platform.

set -euo pipefail

function die() {
  echo "$@" 1>&2 ; exit 1;
}

# Downloads bazelisk to ~/bin as `bazel`.
function install_bazelisk {
  date
  case "$(uname -s)" in
    Darwin) local name=bazelisk-darwin-amd64 ;;
    Linux)  local name=bazelisk-linux-amd64  ;;
    *) die "Unknown OS: $(uname -s)" ;;
  esac
  mkdir -p "$HOME/bin"
  wget --no-verbose -O "$HOME/bin/bazel" \
      "https://github.com/bazelbuild/bazelisk/releases/latest/download/$name"
  chmod u+x "$HOME/bin/bazel"
  if [[ ! ":$PATH:" =~ :"$HOME"/bin/?: ]]; then
    PATH="$HOME/bin:$PATH"
  fi
  which bazel
  bazel version
  date
}

# Check if we have Bazel installed. If not, download Bazelisk.
BAZEL=$(which bazel)
if [ $? -eq 1 ] ; then
  echo "Bazel not found. Installing Bazelisk ..."
  install_bazelisk || die "Failed to install Bazelisk"
  BAZEL=$(which bazel)
fi

# The following step is a kludge. Some of the dependencies of
# `io_bazel_rules_docker` that drag in Go dependencies rely on the hardcoded
# `WORKSPACE` name. See:
#   https://github.com/bazelbuild/bazel-gazelle/issues/678
#   https://github.com/bazelbuild/rules_docker/issues/1139
if [ ! -f WORKSPACE.bazel ] ; then
  die "This script should run from the MozoLM root directory"
fi
touch WORKSPACE || exit 1

# Build the containers for the server and client. Here the host, execution, and
# target platforms are the same. For example, building a Linux executable
# running on an Intel x64 CPU.
echo "Building the containers ..."
PLATFORM=//release:linux-x86_64
for name in "server_async" "client_async" ; do
  ${BAZEL} build -c opt --platforms=${PLATFORM} \
    release:${name}_image || die "Failed to build ${name} image"
done

# Publish the containers in `gcr.io/mozolm-release`.
echo "Releasing the containers ..."
for name in "server_async" "client_async" ; do
  ${BAZEL} run -c opt --platforms=${PLATFORM} \
    release:${name}_publish || die "Failed to publish ${name} image"
done

# Clean up.
rm -f WORKSPACE || exit 1

exit 0
