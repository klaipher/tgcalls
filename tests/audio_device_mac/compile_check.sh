#!/bin/sh
# Syntax-checks tg_owt's audio_device_mac.cc from the submodule with the exact flags of the
# existing webrtc build (needs core-xprojects/webrtc/build/arm64/build.ninja).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
BUILD="$ROOT/core-xprojects/webrtc/build/arm64"
OWT="$(cd "$ROOT/submodules/tg_owt/src" && pwd)"
CMD="$(cd "$BUILD" && ninja -t commands libtg_owt.a | grep 'audio_device_mac.cc' | head -1)"
CMD="$(printf '%s' "$CMD" | sed -e "s| -o [^ ]*| -fsyntax-only|" -e "s| -MD -MT [^ ]* -MF [^ ]*||" -e "s| -c | |" -e "s|$ROOT/core-xprojects/webrtc/build/src|$OWT|g")"
case "$CMD" in *"$OWT/modules/audio_device/mac/audio_device_mac.cc"*) ;; *) echo "command does not compile the submodule source" >&2; exit 1;; esac
cd "$BUILD" && sh -c "$CMD"
echo "audio_device_mac.cc: syntax ok"
