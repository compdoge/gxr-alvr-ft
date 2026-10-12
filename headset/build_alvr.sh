#!/usr/bin/env bash
# Build gxr_alvr (aarch64 Android, API 29). Requires an Android NDK (r29 tested).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
NDK="${NDK:-${ANDROID_NDK_HOME:-}}"
if [ -z "$NDK" ]; then
    echo "set NDK or ANDROID_NDK_HOME to your Android NDK (e.g. android-ndk-r29)" >&2
    exit 1
fi
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) HOST=windows-x86_64 ;;
    Linux)                 HOST=linux-x86_64 ;;
    Darwin)                HOST=darwin-x86_64 ;;
    *)                     HOST=windows-x86_64 ;;
esac
CC="$NDK/toolchains/llvm/prebuilt/$HOST/bin/aarch64-linux-android29-clang"
[ -x "$CC" ] || CC="$CC.cmd"
echo "CC = $CC"
"$CC" -O2 -Wall -Wextra -o "$HERE/gxr_alvr" "$HERE/gxr_alvr.c" -lm
echo "built: $HERE/gxr_alvr"
