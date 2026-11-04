#!/bin/sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${TMPDIR:-/tmp}/tgcalls-stream-render-policy-tests"
xcrun clang++ -std=c++17 -mmacosx-version-min=15.0 -I"$HERE/../../tgcalls" "$HERE/StreamRenderPolicyTests.cpp" -o "$OUT"
"$OUT"
