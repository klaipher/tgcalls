#!/bin/sh
# Builds and runs one header-only policy test on the host. Usage: run_policy_test.sh <test name> <test .cpp>
# The same .cpp files are the sources of the iOS Bazel policy_cc_test targets (submodules/TgVoipWebrtc/BUILD),
# so a test is written once and runs in both apps' flows.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
NAME="$1"
SRC="$2"
OUT="${TMPDIR:-/tmp}/tgcalls-$NAME-tests"
xcrun clang++ -std=c++17 -Wall -Werror -mmacosx-version-min=15.0 -I"$HERE/../tgcalls" "$HERE/$NAME/$SRC" -o "$OUT"
"$OUT"
