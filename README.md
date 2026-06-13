# Top Gear 2 Randomizer

This is the C++ source code used for the randomizer. main.cpp is used for a linux-only command line tool and web.cpp is used for a WASM build. 

See https://volt-software.nl/tg2

## How to run

Builds require linux. Install emscripten, source it, run build-wasm.sh, cd build-wasm, python3 -m http.server 8000 and browse to localhost:8000
