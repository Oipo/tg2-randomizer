#!/usr/bin/env bash
# Build the browser (WASM) randomizer into ./build-wasm and assemble a
# ready-to-deploy bundle there: index.html + tg2-randomizer.js + tg2-randomizer.wasm.
# Upload the contents of ./build-wasm to any static web host.
#
# Requires emsdk to be sourced, or it falls back to the emsdk path baked into
# cmake/Emscripten.cmake.
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR="${BUILD_DIR:-build-wasm}"

cmake -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$(pwd)/cmake/Emscripten.cmake" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
cmake --build "$BUILD_DIR" -j"$(nproc)"

# Put the page next to the generated module so the folder is deployable as-is.
cp web/index.html "$BUILD_DIR/index.html"

echo
echo "Built deployable bundle in: $BUILD_DIR/"
echo "  - index.html"
echo "  - tg2-randomizer.js"
echo "  - tg2-randomizer.wasm"
echo
echo "Test locally:  (cd $BUILD_DIR && python3 -m http.server 8000)  then open http://localhost:8000/"
