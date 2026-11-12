#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Builds every layer-2 scenario, pmedian and lotsizing, into build/, for each backend that is installed:
#   <scenario>_stub     against the lean Concert stub in ../common/stub, so that only the containers are measured.
#   <scenario>_cplex    against CPLEX Concert at $CPLEX_STUDIO_DIR. The default is /opt/ibm/ILOG/CPLEX_Studio2212.
#   <scenario>_mathopt  against an OR-Tools C++ prebuilt at $ORTOOLS_DIR, which solves with HiGHS. The default is
#                       /opt/ortools.
#   <scenario>_highs    against HiGHS's own C++ API at $HIGHS_DIR. The default is the HiGHS in the OR-Tools prebuilt.
# Each backend gets two binaries:
# - <scenario>_<backend> is the dreamor arm, from <scenario>_dreamor.cpp.
# - <scenario>_<backend>_hand holds the hand-written arms, from <scenario>_hand.cpp. It is compiled without dreamor's
#   include path, so a dreamor header in it does not compile. A change to the library therefore leaves this binary
#   byte for byte as it was.
# A backend that is not installed is skipped. CXX picks the compiler. The default is g++, and the compiler must be
# GCC 13 or later, or Clang 18 or later.
# If LAYOUTS=n is set, every binary is also built once per repetition under build/layout/<rep>/, each with a code
# layout of its own that ../common/build_lib.sh makes. run.py --layout on runs repetition r on those copies.
set -e
cd "$(dirname "$0")"
. ../common/build_lib.sh
CXX=${CXX:-g++}
FLAGS="-std=c++20 -O2 -DNDEBUG -march=native -Wall -Wextra"
DREAMOR="-I../../include"  # Only the dreamor arm gets this include path.
STUDIO=${CPLEX_STUDIO_DIR:-/opt/ibm/ILOG/CPLEX_Studio2212}
ARCH=x86-64_linux
ORTOOLS=${ORTOOLS_DIR:-/opt/ortools}
HIGHS=${HIGHS_DIR:-$ORTOOLS}
mkdir -p build
make_layouts

# both <scenario> <backend> <compiler arguments...> builds <scenario>_dreamor.cpp as <scenario>_<backend>, and
# <scenario>_hand.cpp as <scenario>_<backend>_hand. If LAYOUTS is set, it also builds each repetition's copies of both
# under build/layout/<rep>/.
both() {
  scenario=$1
  out=$1_$2
  shift 2
  spawn $CXX $FLAGS $DREAMOR "${scenario}_dreamor.cpp" "$@" -o "build/$out"
  spawn $CXX $FLAGS "${scenario}_hand.cpp" "$@" -o "build/${out}_hand"
  r=0
  while [ $r -lt "$LAYOUTS" ]; do
    spawn $CXX $FLAGS "@build/layout/$r/flags" $DREAMOR "${scenario}_dreamor.cpp" "$@" -o "build/layout/$r/$out"
    spawn $CXX $FLAGS "@build/layout/$r/flags" "${scenario}_hand.cpp" "$@" -o "build/layout/$r/${out}_hand"
    r=$((r + 1))
  done
  echo "building bench/scenario/build/$out and ${out}_hand$([ "$LAYOUTS" = 0 ] ||
    echo ", and $LAYOUTS layouts of each")"
}

for s in pmedian lotsizing; do
  both $s stub -DSCENARIO_STUB -I../common/stub

  if [ -d "$STUDIO/concert/include" ] && [ -d "$STUDIO/cplex/include" ]; then
    # CPLEX's headers come in through -isystem, so that the compiler does not warn about code that is not ours.
    # IL_STD selects Concert's std:: API.
    both $s cplex -DIL_STD -isystem "$STUDIO/concert/include" -isystem "$STUDIO/cplex/include" \
      -L"$STUDIO/cplex/lib/$ARCH/static_pic" -L"$STUDIO/concert/lib/$ARCH/static_pic" \
      -lilocplex -lconcert -lcplex -lm -lpthread -ldl
  else
    echo "skipped ${s}_cplex: no CPLEX Studio at $STUDIO"
  fi

  if [ -f "$ORTOOLS/include/ortools/math_opt/cpp/math_opt.h" ] && [ -d "$ORTOOLS/lib" ]; then
    ABSL=$(for f in "$ORTOOLS"/lib/libabsl_*.so; do b=$(basename "$f" .so); printf ' -l%s' "${b#lib}"; done)
    # shellcheck disable=SC2086
    both $s mathopt -DSCENARIO_MATHOPT -DOR_PROTO_DLL= -isystem "$ORTOOLS/include" \
      -L"$ORTOOLS/lib" -Wl,-rpath,"$ORTOOLS/lib" -lortools -lprotobuf $ABSL
  else
    echo "skipped ${s}_mathopt: no OR-Tools at $ORTOOLS"
  fi

  if [ -f "$HIGHS/include/highs/Highs.h" ]; then
    HIGHS_LIB=$HIGHS/lib
    [ -d "$HIGHS_LIB" ] || HIGHS_LIB=$HIGHS/lib64
    both $s highs -DSCENARIO_HIGHS -isystem "$HIGHS/include/highs" \
      -L"$HIGHS_LIB" -Wl,-rpath,"$HIGHS_LIB" -lhighs
  else
    echo "skipped ${s}_highs: no HiGHS at $HIGHS"
  fi
done

finish
echo "built bench/scenario"
