#!/bin/sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${TMPDIR:-/tmp}/tgcalls-call-system-policy-tests"
xcrun clang++ -std=c++17 -mmacosx-version-min=15.0 -I"$HERE/../../tgcalls" "$HERE/CallSystemPolicyTests.cpp" -o "$OUT"
"$OUT"
