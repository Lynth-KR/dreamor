#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Builds the solver probe (probe.cpp) - not part of the benchmark - for each backend found:
#   ./probe_stub     against the experiment's Concert stub (../common/stub): the containers alone
#   ./probe_concert  against CPLEX Concert at $CPLEX_STUDIO_DIR (default /opt/ibm/ILOG/CPLEX_Studio2212)
#   ./probe_mathopt  against an OR-Tools C++ prebuilt at $ORTOOLS_DIR (default /opt/ortools)
# A backend that is not installed is skipped. CXX picks the compiler (default g++).
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
FLAGS="-std=c++20 -O2 -DNDEBUG -march=native -Wall -Wextra -I../../include"

$CXX $FLAGS -DPROBE_STUB -I../common/stub probe.cpp -o probe_stub
echo "built bench/solver/probe_stub"

STUDIO=${CPLEX_STUDIO_DIR:-/opt/ibm/ILOG/CPLEX_Studio2212}
ARCH=x86-64_linux
if [ -d "$STUDIO/concert/include" ] && [ -d "$STUDIO/cplex/include" ]; then
  # -isystem: CPLEX's own headers are not ours to warn about. IL_STD selects the std:: API.
  $CXX $FLAGS -DIL_STD -isystem "$STUDIO/concert/include" -isystem "$STUDIO/cplex/include" probe.cpp \
    -o probe_concert -L"$STUDIO/cplex/lib/$ARCH/static_pic" -L"$STUDIO/concert/lib/$ARCH/static_pic" \
    -lilocplex -lconcert -lcplex -lm -lpthread -ldl
  echo "built bench/solver/probe_concert ($STUDIO)"
else
  echo "skipped probe_concert: no CPLEX Studio at $STUDIO"
fi

ORTOOLS=${ORTOOLS_DIR:-/opt/ortools}
if [ -f "$ORTOOLS/include/ortools/math_opt/cpp/math_opt.h" ] && [ -d "$ORTOOLS/lib" ]; then
  ABSL=$(for f in "$ORTOOLS"/lib/libabsl_*.so; do b=$(basename "$f" .so); printf ' -l%s' "${b#lib}"; done)
  # shellcheck disable=SC2086
  $CXX $FLAGS -DPROBE_MATHOPT -DOR_PROTO_DLL= -isystem "$ORTOOLS/include" probe.cpp -o probe_mathopt \
    -L"$ORTOOLS/lib" -Wl,-rpath,"$ORTOOLS/lib" -lortools -lprotobuf $ABSL
  echo "built bench/solver/probe_mathopt ($ORTOOLS)"
else
  echo "skipped probe_mathopt: no OR-Tools at $ORTOOLS"
fi
