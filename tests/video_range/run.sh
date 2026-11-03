#!/bin/sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
DARWIN="$HERE/../../tgcalls/platform/darwin"
OUT="${TMPDIR:-/tmp}/tgcalls-video-range-tests"
xcrun clang++ -std=c++17 -fobjc-arc -mmacosx-version-min=12.0 -I"$DARWIN" \
  "$HERE/VideoRangeTests.mm" "$DARWIN/TGRTCScreenVideoRange.mm" \
  -framework Foundation -framework CoreMedia -framework CoreVideo -framework VideoToolbox -o "$OUT"
"$OUT"
