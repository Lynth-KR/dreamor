#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Builds every layer-2 scenario (pmedian.cpp, lotsizing.cpp) for each backend found:
#   ./<scenario>_stub     against the lean Concert stub (../common/stub): the containers alone
#   ./<scenario>_cplex    against CPLEX Concert at $CPLEX_STUDIO_DIR (default /opt/ibm/ILOG/CPLEX_Studio2212)
#   ./<scenario>_mathopt  against an OR-Tools C++ prebuilt at $ORTOOLS_DIR (default /opt/ortools), with HiGHS
# A backend that is not installed is skipped. CXX picks the compiler (default g++; GCC 13+ or Clang 18+).
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
FLAGS="-std=c++20 -O2 -DNDEBUG -march=native -Wall -Wextra -I../../include"
STUDIO=${CPLEX_STUDIO_DIR:-/opt/ibm/ILOG/CPLEX_Studio2212}
ARCH=x86-64_linux
ORTOOLS=${ORTOOLS_DIR:-/opt/ortools}

for s in pmedian lotsizing; do
  $CXX $FLAGS -DSCENARIO_STUB -I../common/stub $s.cpp -o ${s}_stub
  echo "built bench/scenario/${s}_stub"

  if [ -d "$STUDIO/concert/include" ] && [ -d "$STUDIO/cplex/include" ]; then
    # -isystem: CPLEX's own headers are not ours to warn about. IL_STD selects the std:: API.
    $CXX $FLAGS -DIL_STD -isystem "$STUDIO/concert/include" -isystem "$STUDIO/cplex/include" $s.cpp \
      -o ${s}_cplex -L"$STUDIO/cplex/lib/$ARCH/static_pic" -L"$STUDIO/concert/lib/$ARCH/static_pic" \
      -lilocplex -lconcert -lcplex -lm -lpthread -ldl
    echo "built bench/scenario/${s}_cplex ($STUDIO)"
  else
    echo "skipped ${s}_cplex: no CPLEX Studio at $STUDIO"
  fi

  if [ -f "$ORTOOLS/include/ortools/math_opt/cpp/math_opt.h" ] && [ -d "$ORTOOLS/lib" ]; then
    ABSL=$(for f in "$ORTOOLS"/lib/libabsl_*.so; do b=$(basename "$f" .so); printf ' -l%s' "${b#lib}"; done)
    # shellcheck disable=SC2086
    $CXX $FLAGS -DSCENARIO_MATHOPT -DOR_PROTO_DLL= -isystem "$ORTOOLS/include" $s.cpp -o ${s}_mathopt \
      -L"$ORTOOLS/lib" -Wl,-rpath,"$ORTOOLS/lib" -lortools -lprotobuf $ABSL
    echo "built bench/scenario/${s}_mathopt ($ORTOOLS)"
  else
    echo "skipped ${s}_mathopt: no OR-Tools at $ORTOOLS"
  fi
done
