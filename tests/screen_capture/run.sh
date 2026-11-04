#!/bin/sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
DARWIN="$HERE/../../tgcalls/platform/darwin"
OUT="${TMPDIR:-/tmp}/tgcalls-screen-capture-tests"
xcrun clang++ -std=c++17 -fobjc-arc -mmacosx-version-min=15.0 -I"$DARWIN" \
  "$HERE/ScreenCaptureConfigurationTests.mm" "$DARWIN/ScreenCaptureKitConfiguration.mm" "$DARWIN/ScreenCaptureKitSources.mm" \
  -framework Foundation -framework AppKit -framework QuartzCore -framework IOSurface -framework CoreMedia -framework CoreVideo -framework CoreGraphics -framework ScreenCaptureKit -o "$OUT"
"$OUT"
