#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Builds build/catalog from dreamor.cpp, against the dreamor headers in ../../include, and build/catalog_hand from
# hand.cpp, without them. For k = 8 it builds build/catalog8 and build/catalog_hand8 the same way. Every build uses the
# lean Concert stub in ../common/stub. CXX picks the compiler: GCC 13 or later, or Clang 18 or later. The default is
# g++.
# With LAYOUTS=n, the script also builds every binary once for each of n repetitions, under build/layout/<rep>/, each
# with a code layout of its own (../common/build_lib.sh). run.py --layout on runs repetition r on the binaries of r.
set -e
cd "$(dirname "$0")"
. ../common/build_lib.sh
CXX=${CXX:-g++}
F="-std=c++20 -O2 -DNDEBUG -march=native -Wall -Wextra -I../common/stub"
mkdir -p build
make_layouts
# The dreamor arm is built with the library's include path. The hand-written arms are built without it, so that a
# dreamor header in them fails the build, and a change to the library leaves their binaries the same, byte for byte.
# k = 8 has builds of its own, in which the hand-written arms and the samples hold rows 8 wide, as data.hpp describes.
# each <output> <source> <compiler arguments...> builds build/<output>, and a copy for each repetition under
# build/layout/<rep>/.
each() {
  out=$1
  src=$2
  shift 2
  spawn $CXX $F "$@" "$src" -o "build/$out"
  r=0
  while [ $r -lt "$LAYOUTS" ]; do
    spawn $CXX $F "@build/layout/$r/flags" "$@" "$src" -o "build/layout/$r/$out"
    r=$((r + 1))
  done
}
each catalog dreamor.cpp -I../../include
each catalog8 dreamor.cpp -I../../include -DCATALOG_MAX_COLUMNS=8
each catalog_hand hand.cpp
each catalog_hand8 hand.cpp -DCATALOG_MAX_COLUMNS=8
finish
echo "built bench/catalog/build/catalog, catalog8, catalog_hand and catalog_hand8$([ "$LAYOUTS" = 0 ] ||
  echo ", and $LAYOUTS layouts of each")"
