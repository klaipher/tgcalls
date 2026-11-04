#!/bin/sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${TMPDIR:-/tmp}/tgcalls-encoder-mode-policy-tests"
xcrun clang++ -std=c++17 -mmacosx-version-min=15.0 -I"$HERE/../../tgcalls" "$HERE/EncoderModePolicyTests.cpp" -o "$OUT"
"$OUT"
