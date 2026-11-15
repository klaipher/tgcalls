#!/bin/sh
# Host test for voice_enhance on macOS. WebRTC APM is replaced by a pass-through stand-in (stub/), RNNoise is built from
# source. TG_RNNOISE: the RNNoise fork checkout (default ../../../rnnoise, next to this fork in wt/forks).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
RNN="${TG_RNNOISE:-$HERE/../../../rnnoise}"
OUT="${TMPDIR:-/tmp}/tgcalls-voice-enhance-tests"
mkdir -p "$OUT"
for f in denoise kiss_fft pitch celt_lpc rnn nnet nnet_default parse_lpcnet_weights rnnoise_tables rnnoise_data; do
  xcrun clang -c -O2 -I"$RNN/include" -I"$RNN/src" "$RNN/src/$f.c" -o "$OUT/$f.o"
done
xcrun clang++ -std=c++17 -O1 -Wall -Werror -I"$HERE/.." -I"$HERE/../../voice_enhance/.." -I"$HERE/stub" -I"$RNN/include" \
  "$HERE/VoiceEnhanceTests.cpp" "$HERE/../../voice_enhance/voice_enhance.cc" "$OUT"/*.o -o "$OUT/voice_enhance_tests"
"$OUT/voice_enhance_tests"
