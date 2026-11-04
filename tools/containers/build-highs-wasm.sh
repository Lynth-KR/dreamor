#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Builds HiGHS for WebAssembly with the Emscripten it runs under and installs it at PREFIX: a static library, built
# the way WASM=1 tests/run_tests.sh builds the tests (-fwasm-exceptions, -pthread), so the two link. CI's
# wasm-highs job runs it into its cache, and tools/containers/highs-wasm.Containerfile into the local CI's image.
#
#   build-highs-wasm.sh PREFIX
#
# HIGHS_VERSION is the HiGHS the OR-Tools prebuilt of the mathopt job ships.
set -eu
prefix=$1
HIGHS_VERSION=1.12.0
src=$(mktemp -d)
trap 'rm -rf "$src"' EXIT
curl -sSfL "https://github.com/ERGO-Code/HiGHS/archive/refs/tags/v$HIGHS_VERSION.tar.gz" |
  tar xz -C "$src" --strip-components=1
emcmake cmake -S "$src" -B "$src/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DBUILD_CXX_EXE=OFF -DBUILD_EXAMPLES=OFF -DZLIB=OFF \
  "-DCMAKE_CXX_FLAGS=-fwasm-exceptions -pthread" "-DCMAKE_C_FLAGS=-pthread" > "$src/configure.log"
cmake --build "$src/build" -j"$(nproc)" > "$src/build.log"
cmake --install "$src/build" > /dev/null
