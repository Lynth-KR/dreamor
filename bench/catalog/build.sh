#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Builds build/catalog against the dreamor headers in ../../include, build/catalog_hand without them, both on
# the lean Concert stub in ../common/stub. CXX picks the compiler (default g++; GCC 13+ or Clang 18+).
# LAYOUTS=n also builds every binary once per repetition, under build/layout/<rep>/, each with a code layout
# of its own (../common/build_lib.sh); run.py --layout on runs repetition r on those.
set -e
cd "$(dirname "$0")"
. ../common/build_lib.sh
CXX=${CXX:-g++}
F="-std=c++20 -O2 -DNDEBUG -march=native -Wall -Wextra -I../../include -I../common/stub"
mkdir -p build
make_layouts
# The dreamor arm on build/catalog; the hand-written arms on build/catalog_hand, which includes no dreamor
# header but the logger, so that a change to the library leaves it byte for byte as it was. k = 8 on builds
# of their own: rows 8 wide in the hand-written arms and the samples (data.hpp).
# each <output> <compiler arguments...>: build/<output>, and each repetition's copy under build/layout/<rep>/.
each() {
  out=$1
  shift
  spawn $CXX $F "$@" catalog.cpp -o "build/$out"
  r=0
  while [ $r -lt "$LAYOUTS" ]; do
    spawn $CXX $F "@build/layout/$r/flags" "$@" catalog.cpp -o "build/layout/$r/$out"
    r=$((r + 1))
  done
}
each catalog -DCATALOG_DREAMOR=1
each catalog8 -DCATALOG_DREAMOR=1 -DCATALOG_MAX_COLUMNS=8
each catalog_hand -DCATALOG_DREAMOR=0
each catalog_hand8 -DCATALOG_DREAMOR=0 -DCATALOG_MAX_COLUMNS=8
finish
echo "built bench/catalog/build/catalog, catalog8, catalog_hand and catalog_hand8$([ "$LAYOUTS" = 0 ] ||
  echo ", and $LAYOUTS layouts of each")"
