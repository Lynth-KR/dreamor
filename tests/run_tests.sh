#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Builds and runs the opt tests.
#
#   ./run_tests.sh                 Concert stub, no CPLEX needed (tests/stub).
#   ./run_tests.sh --cplex [DIR]   real CPLEX Studio at DIR (default $CPLEX_STUDIO_DIR, else
#                                  /opt/ibm/ILOG/CPLEX_Studio2212).
#   ./run_tests.sh --mathopt [DIR] the MathOpt backend against an OR-Tools C++ prebuilt
#                                  unpacked at DIR (default $ORTOOLS_DIR, else /opt/ortools):
#                                  tests/mathopt/*.cpp (solving with HiGHS),
#                                  examples/production_planning_mathopt.cpp, and the
#                                  OR-Tools samples ported in examples/mathopt_samples/.
#
# Stub mode runs every test_*.cpp, checks that every compile_fail/*.cpp is rejected, and
# syntax-checks cplex/*.cpp against a stand-in IloCplex. CPLEX mode runs the tests that use
# only public Concert API (test_index_set and cplex/*.cpp, which solve), builds and runs
# examples/production_planning.cpp, and checks the compile_fail cases against the real headers.
# MathOpt mode builds and runs only the MathOpt tests and example; test_cross_backend there
# also builds the Concert model (on the stub) and compares the two models' constraints.
#
# CXX overrides the compiler (default g++, C++20). OPT overrides the optimization flags
# (default -O1 -g; CI also runs -O3 -DNDEBUG). SAN=1 adds AddressSanitizer and
# UndefinedBehaviorSanitizer, in stub mode only: the OR-Tools and CPLEX libraries are not
# instrumented. JOBS sets how many compiles run at once (default: every core); the tests then
# run and report one at a time, in a fixed order.
set -u
cd "$(dirname "$0")"
CXX=${CXX:-g++}
BUILD=build
MODE=stub
if [ "${1:-}" = "--cplex" ]; then
  MODE=cplex
  STUDIO=${2:-${CPLEX_STUDIO_DIR:-/opt/ibm/ILOG/CPLEX_Studio2212}}
elif [ "${1:-}" = "--mathopt" ]; then
  MODE=mathopt
  ORTOOLS=${2:-${ORTOOLS_DIR:-/opt/ortools}}
fi
mkdir -p "$BUILD"
OPT=${OPT:--O1 -g}
COMMON="-std=c++20 $OPT -I../include -I../examples -I."
if [ "${SAN:-}" = 1 ]; then
  [ "$MODE" = stub ] || { echo "SAN=1 runs in stub mode only"; exit 2; }
  COMMON="$COMMON -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
fi
# dreamor's own code - the library, the tests, the examples - is held to STRICT: a header
# that warns here would warn in a user's strict build. The CMake target adds no warning flags.
# LENIENT is for code that only uses dreamor (the ported OR-Tools samples) and for the
# compile_fail cases, whose first error must be the library's message, not a warning.
STRICT="-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wundef
        -Wnull-dereference -Wdouble-promotion -Wformat-security -Wmissing-declarations -Wfloat-equal
        -Wcast-align -Wnon-virtual-dtor -Werror"
case "$CXX" in *clang*) STRICT="$STRICT -Wmissing-prototypes" ;; esac  # C only in GCC
LENIENT="-Wall -Wextra"
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

if [ "$MODE" = stub ]; then
  FLAGS="$COMMON -Istub"
  LIBS=""
  TESTS=$(ls test_*.cpp)
elif [ "$MODE" = mathopt ]; then
  if [ ! -f "$ORTOOLS/include/ortools/math_opt/cpp/math_opt.h" ] || [ ! -d "$ORTOOLS/lib" ]; then
    echo "OR-Tools not found at $ORTOOLS (pass the directory: ./run_tests.sh --mathopt DIR)"; exit 2
  fi
  # OR_PROTO_DLL: the prebuilt's headers expect it defined (empty). -isystem: OR-Tools'
  # headers are not ours to warn about. Abseil is linked library by library.
  ABSL=$(for f in "$ORTOOLS"/lib/libabsl_*.so; do b=$(basename "$f" .so); printf ' -l%s' "${b#lib}"; done)
  FLAGS="$COMMON -Istub -DOR_PROTO_DLL= -isystem $ORTOOLS/include"
  LIBS="-L$ORTOOLS/lib -Wl,-rpath,$ORTOOLS/lib -lortools -lprotobuf$ABSL"
  TESTS="$(ls mathopt/test_*.cpp) ../examples/production_planning_mathopt.cpp"
  echo "OR-Tools: $ORTOOLS"
else
  ARCH=x86-64_linux
  if [ ! -d "$STUDIO/concert/include" ] || [ ! -d "$STUDIO/cplex/include" ]; then
    echo "CPLEX Studio not found at $STUDIO (pass the directory: ./run_tests.sh --cplex DIR)"; exit 2
  fi
  # -isystem: CPLEX's own headers are not ours to warn about. IL_STD selects the std:: API.
  FLAGS="$COMMON -DIL_STD -isystem $STUDIO/concert/include -isystem $STUDIO/cplex/include"
  LIBS="-L$STUDIO/cplex/lib/$ARCH/static_pic -L$STUDIO/concert/lib/$ARCH/static_pic -lilocplex -lconcert -lcplex -lm -lpthread -ldl"
  TESTS="test_index_set.cpp $(ls cplex/test_*.cpp) ../examples/production_planning.cpp"
  echo "CPLEX Studio: $STUDIO"
fi

CF=compile_fail
[ "$MODE" = mathopt ] && CF=mathopt/compile_fail

# Every compile first, JOBS at a time: one job is KIND NAME SOURCE, and it leaves its
# diagnostics in $BUILD/<log>.log and its exit status in $BUILD/<log>.rc, so the results can
# be read back below in a fixed order whatever order the jobs finished in.
compile_job='
  kind=$1 name=$2 src=$3
  case $kind in
    build)  log=$name;        set -- $FLAGS $STRICT "$src" -o "$BUILD/$name" $LIBS ;;
    syntax) log=syntax_$name; set -- $FLAGS $STRICT -fsyntax-only "$src" ;;
    sample) log=$name;        set -- $FLAGS $LENIENT "$src" -o "$BUILD/$name" $LIBS ;;
    cf)     log=cf_$name;     set -- $FLAGS $LENIENT -I$CF -x c++ -fsyntax-only "$src" ;;
  esac
  $CXX "$@" 2> "$BUILD/$log.log"
  echo $? > "$BUILD/$log.rc"'
export CXX FLAGS STRICT LENIENT LIBS BUILD CF
rm -f "$BUILD"/*.rc
{
  for t in $TESTS; do echo "build $(basename "${t%.cpp}") $t"; done
  if [ "$MODE" = stub ]; then
    for t in cplex/*.cpp ../examples/production_planning.cpp; do echo "syntax $(basename "${t%.cpp}") $t"; done
  fi
  if [ "$MODE" = mathopt ]; then
    for t in ../examples/mathopt_samples/*.cc; do echo "sample $(basename "${t%.cc}") $t"; done
  fi
  echo "cf control $CF/control.cc"
  for t in $CF/*.cpp; do echo "cf $(basename "${t%.cpp}") $t"; done
} | xargs -P "$JOBS" -L 1 sh -c "$compile_job" sh
compiled() { [ "$(cat "$BUILD/$1.rc" 2> /dev/null)" = 0 ]; }

status=0
for t in $TESTS; do
  name=$(basename "${t%.cpp}")
  if ! compiled "$name"; then
    echo "$name: BUILD FAILED (see tests/$BUILD/$name.log)"; status=1; continue
  fi
  if [ "$name" = production_planning ] || [ "$name" = production_planning_mathopt ]; then
    label=example; [ "$name" = production_planning_mathopt ] && label=example_mathopt
    "$BUILD/$name" > "$BUILD/$name.out" && printf '%-16s ok  (solved; plan in tests/%s/%s.out)\n' "$label" "$BUILD" "$name" || status=1
  else
    "$BUILD/$name" || status=1
  fi
done

if [ "$MODE" = stub ]; then
  echo "mathopt/*.cpp: skipped (needs OR-Tools: ./run_tests.sh --mathopt DIR)"
  for t in cplex/*.cpp ../examples/production_planning.cpp; do
    name=$(basename "${t%.cpp}")
    if compiled "syntax_$name"; then
      echo "$name: syntax ok (runs only with --cplex)"
    else
      echo "$name: SYNTAX FAILED (see tests/$BUILD/syntax_$name.log)"; status=1
    fi
  done
fi

# The OR-Tools MathOpt samples ported to dreamor: each is built and run on its small
# instance (time_indexed_scheduling on its fixed test data, not a random one).
if [ "$MODE" = mathopt ]; then
  for t in ../examples/mathopt_samples/*.cc; do
    name=$(basename "${t%.cc}")
    args=""
    [ "$name" = time_indexed_scheduling ] && args="--use_test_data"
    if ! compiled "$name"; then
      echo "$name: BUILD FAILED (see tests/$BUILD/$name.log)"; status=1; continue
    fi
    if NO_COLOR=1 "$BUILD/$name" $args > "$BUILD/$name.out" 2>&1; then
      printf '%-24s ok  (solved; log in tests/%s/%s.out)\n' "sample $name" "$BUILD" "$name"
    else
      echo "$name: RUN FAILED (see tests/$BUILD/$name.out)"; status=1
    fi
  done
fi

# Code that must not compile: compile_fail/ against Concert (stub or real), mathopt/compile_fail/
# against OR-Tools.
if ! compiled cf_control; then
  echo "$CF/control.cc: FAILED to compile - the cases below would be meaningless (see tests/$BUILD/cf_control.log)"
  status=1
fi
# A case may say how it must be rejected: a `// expect: <text>` line, matched against the
# compiler's first error and its notes - what a user reads before the next error. Cases
# without one only have to be rejected.
for t in $CF/*.cpp; do
  name=$(basename "${t%.cpp}")
  expect=$(sed -n 's|^// expect: ||p' "$t" | head -n 1)
  if compiled "cf_$name"; then
    echo "$CF/$name: COMPILED but must not"; status=1
  elif [ -n "$expect" ] && ! awk '/error:/ { n++ } n == 1' "$BUILD/cf_$name.log" | grep -qF -- "$expect"; then
    echo "$CF/$name: rejected, but the first error does not say \"$expect\" (see tests/$BUILD/cf_$name.log)"; status=1
  elif [ -n "$expect" ]; then
    echo "$CF/$name: rejected with its message (ok)"
  else
    echo "$CF/$name: rejected (ok)"
  fi
done

[ $status -eq 0 ] && echo "ALL PASSED ($MODE)" || echo "SOME FAILED ($MODE)"
exit $status
