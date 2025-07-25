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

"""Repository rule for Android SDK and NDK autoconfiguration.

`android_configure` depends on the following environment variables:

  * `ANDROID_NDK_HOME`: Location of Android NDK root.
  * `ANDROID_SDK_HOME`: Location of Android SDK root.
  * `ANDROID_SDK_API_LEVEL`: Desired Android SDK API version.
  * `ANDROID_NDK_API_LEVEL`: Desired Android NDK API version.
  * `ANDROID_BUILD_TOOLS_VERSION`: Desired Android build tools version.
"""

# TODO: Move logic for getting default values for the env variables
# from configure.py script into this rule.

_ANDROID_NDK_HOME = "ANDROID_NDK_HOME"
_ANDROID_SDK_HOME = "ANDROID_SDK_HOME"
_ANDROID_NDK_API_VERSION = "ANDROID_NDK_API_LEVEL"
_ANDROID_SDK_API_VERSION = "ANDROID_SDK_API_LEVEL"
_ANDROID_BUILD_TOOLS_VERSION = "ANDROID_BUILD_TOOLS_VERSION"

_ANDROID_SDK_REPO_TEMPLATE = """
    native.android_sdk_repository(
        name="androidsdk",
        path="%s",
        api_level=%s,
        build_tools_version="%s",
    )
"""

_ANDROID_NDK_REPO_TEMPLATE = """
    native.android_ndk_repository(
        name="androidndk",
        path="%s",
        api_level=%s,
    )
"""

def _android_autoconf_impl(repository_ctx):
    """Implementation of the android_autoconf repository rule."""
    sdk_home = repository_ctx.os.environ.get(_ANDROID_SDK_HOME)
    sdk_api_level = repository_ctx.os.environ.get(_ANDROID_SDK_API_VERSION)
    build_tools_version = repository_ctx.os.environ.get(
        _ANDROID_BUILD_TOOLS_VERSION,
    )
    ndk_home = repository_ctx.os.environ.get(_ANDROID_NDK_HOME)
    ndk_api_level = repository_ctx.os.environ.get(_ANDROID_NDK_API_VERSION)

    sdk_rule = ""
    if all([sdk_home, sdk_api_level, build_tools_version]):
        sdk_rule = _ANDROID_SDK_REPO_TEMPLATE % (
            sdk_home,
            sdk_api_level,
            build_tools_version,
        )

    ndk_rule = ""
    if all([ndk_home, ndk_api_level]):
        ndk_rule = _ANDROID_NDK_REPO_TEMPLATE % (ndk_home, ndk_api_level)

    if ndk_rule == "" and sdk_rule == "":
        sdk_rule = "pass"

    repository_ctx.template(
        "BUILD",
        Label("//extra/android:android_configure.BUILD.tpl"),
    )
    repository_ctx.template(
        "android.bzl",
        Label("//extra/android:android.bzl.tpl"),
        substitutions = {
            "MAYBE_ANDROID_SDK_REPOSITORY": sdk_rule,
            "MAYBE_ANDROID_NDK_REPOSITORY": ndk_rule,
        },
    )

android_configure = repository_rule(
    implementation = _android_autoconf_impl,
    environ = [
        _ANDROID_SDK_API_VERSION,
        _ANDROID_NDK_API_VERSION,
        _ANDROID_BUILD_TOOLS_VERSION,
        _ANDROID_NDK_HOME,
        _ANDROID_SDK_HOME,
    ],
)

# buildifier: disable=no-effect
"""Writes Android SDK and NDK rules.

Add the following to your WORKSPACE FILE:

```python
android_configure(name = "local_config_android")
```

Args:
  name: A unique name for this workspace rule.
"""
