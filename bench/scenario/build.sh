#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Builds every layer-2 scenario (pmedian.cpp, lotsizing.cpp) for each backend found, into build/:
#   <scenario>_stub     against the lean Concert stub (../common/stub): the containers alone
#   <scenario>_cplex    against CPLEX Concert at $CPLEX_STUDIO_DIR (default /opt/ibm/ILOG/CPLEX_Studio2212)
#   <scenario>_mathopt  against an OR-Tools C++ prebuilt at $ORTOOLS_DIR (default /opt/ortools), with HiGHS
#   <scenario>_highs    against HiGHS's own C++ API at $HIGHS_DIR (default: the HiGHS in that prebuilt)
# each twice: <scenario>_<backend> is the dreamor arm; <scenario>_<backend>_hand the hand-written arms, which
# include no dreamor header but the logger, so that a change to the library leaves them byte for byte as they were.
# A backend that is not installed is skipped. CXX picks the compiler (default g++; GCC 13+ or Clang 18+).
# LAYOUTS=n also builds every binary once per repetition, under build/layout/<rep>/, each with a code layout of its own
# (../common/build_lib.sh); run.py --layout on runs repetition r on those.
set -e
cd "$(dirname "$0")"
. ../common/build_lib.sh
CXX=${CXX:-g++}
FLAGS="-std=c++20 -O2 -DNDEBUG -march=native -Wall -Wextra -I../../include"
STUDIO=${CPLEX_STUDIO_DIR:-/opt/ibm/ILOG/CPLEX_Studio2212}
ARCH=x86-64_linux
ORTOOLS=${ORTOOLS_DIR:-/opt/ortools}
HIGHS=${HIGHS_DIR:-$ORTOOLS}
mkdir -p build
make_layouts

# both <output> <compiler arguments...>: the dreamor build as <output>, the hand-written one as <output>_hand, and
# each repetition's copies of both under build/layout/<rep>/.
both() {
  out=$1
  shift
  spawn $CXX $FLAGS -DSCENARIO_DREAMOR=1 "$@" -o "build/$out"
  spawn $CXX $FLAGS -DSCENARIO_DREAMOR=0 "$@" -o "build/${out}_hand"
  r=0
  while [ $r -lt "$LAYOUTS" ]; do
    spawn $CXX $FLAGS "@build/layout/$r/flags" -DSCENARIO_DREAMOR=1 "$@" -o "build/layout/$r/$out"
    spawn $CXX $FLAGS "@build/layout/$r/flags" -DSCENARIO_DREAMOR=0 "$@" -o "build/layout/$r/${out}_hand"
    r=$((r + 1))
  done
  echo "building bench/scenario/build/$out and ${out}_hand$([ "$LAYOUTS" = 0 ] ||
    echo ", and $LAYOUTS layouts of each")"
}

for s in pmedian lotsizing; do
  both ${s}_stub -DSCENARIO_STUB -I../common/stub $s.cpp

  if [ -d "$STUDIO/concert/include" ] && [ -d "$STUDIO/cplex/include" ]; then
    # -isystem: CPLEX's own headers are not ours to warn about. IL_STD selects the std:: API.
    both ${s}_cplex -DIL_STD -isystem "$STUDIO/concert/include" -isystem "$STUDIO/cplex/include" $s.cpp \
      -L"$STUDIO/cplex/lib/$ARCH/static_pic" -L"$STUDIO/concert/lib/$ARCH/static_pic" \
      -lilocplex -lconcert -lcplex -lm -lpthread -ldl
  else
    echo "skipped ${s}_cplex: no CPLEX Studio at $STUDIO"
  fi

  if [ -f "$ORTOOLS/include/ortools/math_opt/cpp/math_opt.h" ] && [ -d "$ORTOOLS/lib" ]; then
    ABSL=$(for f in "$ORTOOLS"/lib/libabsl_*.so; do b=$(basename "$f" .so); printf ' -l%s' "${b#lib}"; done)
    # shellcheck disable=SC2086
    both ${s}_mathopt -DSCENARIO_MATHOPT -DOR_PROTO_DLL= -isystem "$ORTOOLS/include" $s.cpp \
      -L"$ORTOOLS/lib" -Wl,-rpath,"$ORTOOLS/lib" -lortools -lprotobuf $ABSL
  else
    echo "skipped ${s}_mathopt: no OR-Tools at $ORTOOLS"
  fi

  if [ -f "$HIGHS/include/highs/Highs.h" ]; then
    HIGHS_LIB=$HIGHS/lib
    [ -d "$HIGHS_LIB" ] || HIGHS_LIB=$HIGHS/lib64
    both ${s}_highs -DSCENARIO_HIGHS -isystem "$HIGHS/include/highs" $s.cpp \
      -L"$HIGHS_LIB" -Wl,-rpath,"$HIGHS_LIB" -lhighs
  else
    echo "skipped ${s}_highs: no HiGHS at $HIGHS"
  fi
done

finish
echo "built bench/scenario"
