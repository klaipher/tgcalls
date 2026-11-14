#!/bin/sh
# Header-only tests for tg_owt's macOS audio device options, plus a syntax
# check of audio_device_mac.cc itself (no libraries needed).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OWT="${TG_OWT:-$HERE/../../../webrtc}"
OUT="${TMPDIR:-/tmp}/tgcalls-audio-device-mac-tests"
xcrun clang++ -std=c++20 -O1 -I"$OWT" "$HERE/AudioDeviceMacOptionsTests.cpp" -o "$OUT"
"$OUT"
