#!/bin/sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
TGCALLS="$HERE/../../tgcalls"
OWT="${TG_OWT:-$HERE/../../../webrtc}"
LIB="${TG_OWT_LIB:-$HERE/../../../../core-xprojects/webrtc/build/arm64/libtg_owt.a}"
OUT="${TMPDIR:-/tmp}/tgcalls-log-sink-tests"
xcrun clang++ -std=c++20 -O1 -DWEBRTC_MAC -DWEBRTC_POSIX -DNDEBUG ${EXTRA_DEFINES} -Wno-deprecated-builtins \
  -I"$TGCALLS" -I"$OWT" -I"$OWT/third_party/abseil-cpp" \
  "$HERE/LogSinkImplTests.cpp" "$TGCALLS/LogSinkImpl.cpp" "$LIB" \
  -framework Foundation -framework CoreFoundation -o "$OUT"
"$OUT"
