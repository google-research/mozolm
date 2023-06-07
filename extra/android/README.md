# Android cross-compile setup

In order for the Bazel toolchain configurations in this directory to be
effective, i.e. actually register the Android cross-compile toolchains,
following combination of environment variables need to be defined:

1.  `ANDROID_NDK_HOME` and `ANDROID_NDK_API_LEVEL`, and/or
1.  `ANDROID_SDK_HOME` and `ANDROID_SDK_API_LEVEL`,

depending on whether NDK and/or SDK was installed.

IMPORTANT: In the absence of the above, passing Android Bazel configuration
flags to Bazel will result in a build error.
