#!/bin/sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
DARWIN="$HERE/../../tgcalls/platform/darwin"
GROUP="$HERE/../../tgcalls/group"
OUT="${TMPDIR:-/tmp}/tgcalls-video-support-tests"
xcrun clang++ -std=c++17 -fobjc-arc -mmacosx-version-min=15.0 -I"$DARWIN" -I"$GROUP" -I"$HERE/../../tgcalls" \
  "$HERE/VideoSupportTests.mm" "$DARWIN/TGRTCPixelTransfer.mm" \
  -framework Foundation -framework CoreVideo -framework CoreMedia -framework VideoToolbox -framework IOSurface -o "$OUT"
"$OUT"
