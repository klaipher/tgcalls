#!/bin/sh
# Build the reference call core (abi1) to WebAssembly, then embed it as a C++ array.
# Same compiler flags as the iOS genrule :reference_core_wasm (submodules/TgVoipWebrtc/BUILD):
#   -Oz -flto, no exceptions/rtti, reactor exec model, --strip-all.
# The wasm sha256 must equal the iOS genrule output, so keep the two in step.
#
# Usage: tools/build_wasm_core.sh WASI_SDK_DIR OUT_DIR
#   WASI_SDK_DIR  unpacked wasi-sdk (contains bin/clang++ and share/wasi-sysroot)
#   OUT_DIR       receives reference-core-abi1.wasm and ReferenceCoreEmbedded.cpp
set -eu
if [ "$#" -ne 2 ]; then
    echo "usage: $0 WASI_SDK_DIR OUT_DIR" >&2
    exit 2
fi
SDK_DIR=$1
OUT_DIR=$2
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/tgcalls"
mkdir -p "$OUT_DIR"
"$SDK_DIR/bin/clang++" --target=wasm32-wasip1 --sysroot="$SDK_DIR/share/wasi-sysroot" \
    -std=c++17 -Oz -flto -fno-exceptions -fno-rtti \
    -mexec-model=reactor -Wl,--no-entry,--strip-all \
    -I"$SRC" \
    "$SRC/v2wasm/ReferenceCallCore.cpp" \
    "$SRC/v2wasm/SignalingFraming.cpp" \
    "$SRC/v2wasm/CoreGzip.cpp" \
    "$SRC/v2wasm/wasm_module_entry.cpp" \
    "$SRC/third-party/json11.cpp" \
    "$SRC/v2wasm/reference_core_factory.cpp" \
    -o "$OUT_DIR/reference-core-abi1.wasm"
python3 "$ROOT/tools/wasm_to_source.py" "$OUT_DIR/reference-core-abi1.wasm" "$OUT_DIR/ReferenceCoreEmbedded.cpp"
shasum -a 256 "$OUT_DIR/reference-core-abi1.wasm"
