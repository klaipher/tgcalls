#!/bin/sh
# List tgcalls .cpp files that Android's CMake does not mention.
# Usage: tools/check_android_cmake.sh PATH/TO/TMessagesProj/jni/voip/CMakeLists.txt
# Excluded: other platforms' layers, host tests, the wasm module entry and tools.
# Every file printed must be intentionally absent (host tests are); anything else is a missing source.
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: $0 ANDROID_VOIP_CMAKELISTS" >&2
    exit 2
fi
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CMAKE=$1
git -C "$ROOT" ls-files 'tgcalls/*.cpp' \
    | grep -v -e '^tgcalls/platform/' -e '^tgcalls/v2wasm/' -e '^tgcalls/tools/' \
    | while read -r file; do
        if ! grep -q "tgcalls/$file" "$CMAKE"; then
            echo "$file"
        fi
    done
