#!/bin/sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OPUS="${OPUS_LIB:-$(cd "$(dirname "$0")" && pwd)/../../../../core-xprojects/libopus/build/libopus/lib/libopus.a}"
TGCALLS="$HERE/../../tgcalls"
OWT="${TG_OWT:-$HERE/../../../tg_owt}"
LIB="${TG_OWT_LIB:-$HERE/../../../../core-xprojects/webrtc/build/arm64/libtg_owt.a}"
OUT="${TMPDIR:-/tmp}/tgcalls-audio-policy-tests"
xcrun clang++ -std=c++20 -O1 -DWEBRTC_MAC -DWEBRTC_POSIX -DNDEBUG -Wno-deprecated-builtins \
  -I"$TGCALLS" -I"$OWT/src" -I"$OWT/src/third_party/abseil-cpp" \
  "$HERE/AudioPolicyTests.cpp" "$LIB" "$OPUS" \
  -framework Foundation -framework CoreFoundation -framework AudioToolbox -framework CoreAudio -o "$OUT"
"$OUT"
