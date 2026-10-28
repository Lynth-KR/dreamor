#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Runs what CI runs (.github/workflows/ci.yml) on this machine, over one commit checked out
# apart: what a push would publish, not the working tree. It needs every tool CI installs and
# stops before running anything if one is missing - a partial run is not a pass.
#
#   tools/ci_local.sh         HEAD
#   tools/ci_local.sh REV     another commit (the pre-push hook passes the one being pushed)
#
# ORTOOLS_DIR is the OR-Tools C++ prebuilt CI downloads (v9.15; default /opt/ortools). The jobs
# run one after another, each on every core; format and tidy first, and nothing else if either
# fails, as on CI. Logs go to a temporary folder, kept if a job fails.
# The jobs are written down twice: change this together with ci.yml.
set -eu
cd "$(dirname "$0")/.."
rev=$(git rev-parse --verify "${1:-HEAD}^{commit}")
ORTOOLS_DIR=${ORTOOLS_DIR:-/opt/ortools}
GCC_IMAGE=docker.io/library/gcc:16

# The machine: every tool CI installs. uv runs the pinned formatters and clang-tidy (uvx), at the
# versions tools/format.sh, tools/tidy.sh and ruff.toml pin.
missing=
lack() { missing="$missing  $1\n"; }
for tool in g++-13 clang++-18 clang++-23 cmake podman python3 uv; do
  command -v "$tool" > /dev/null 2>&1 || lack "$tool"
done
if command -v podman > /dev/null 2>&1 && ! podman image exists "$GCC_IMAGE"; then
  lack "the $GCC_IMAGE image: podman pull $GCC_IMAGE"
fi
[ -f "$ORTOOLS_DIR/include/ortools/math_opt/cpp/math_opt.h" ] || lack "OR-Tools at ORTOOLS_DIR ($ORTOOLS_DIR)"
if [ -n "$missing" ]; then
  printf 'ci_local: this machine lacks what CI has:\n%b' "$missing" >&2
  exit 1
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/dreamor-ci.XXXXXX")
src=$work/src
logs=$work/logs
mkdir "$logs"
git worktree add -q --detach "$src" "$rev"
failed=
finish() {
  git worktree remove --force "$src" 2> /dev/null || true
  if [ -n "$failed" ]; then
    echo "ci_local: FAILED:$failed - logs in $logs"
  else
    rm -rf "$work"
  fi
}
trap finish EXIT
trap 'failed=" interrupted"; exit 130' INT TERM

echo "ci_local: $(git log -1 --format='%h %s' "$rev")"
job() {  # job NAME COMMAND: runs COMMAND in the checkout, its output in NAME.log
  name=$1
  start=$(date +%s)
  if (cd "$src" && sh -c "$2") > "$logs/$name.log" 2>&1 < /dev/null; then
    printf '  %-32s ok      %4ss\n' "$name" "$(($(date +%s) - start))"
  else
    printf '  %-32s FAILED  %4ss\n' "$name" "$(($(date +%s) - start))"
    failed="$failed $name"
  fi
}
export ORTOOLS_DIR

job format 'tools/format.sh --check'
job tidy 'python3 tools/gen_compile_commands.py && tools/tidy.sh'
[ -z "$failed" ] || exit 1

# GCC 13 at -O1 -g is sanitize (1); the compile-time checks run once per compiler. As on CI.
job "stub (g++-13, -O3 -DNDEBUG)" "CXX=g++-13 OPT='-O3 -DNDEBUG' tests/run_tests.sh"
job "stub (clang++-18, -O1 -g)" "CXX=clang++-18 OPT='-O1 -g' tests/run_tests.sh"
job "stub (clang++-18, -O3 -DNDEBUG)" "CXX=clang++-18 OPT='-O3 -DNDEBUG' COMPILE_CHECKS=0 tests/run_tests.sh"
for san in 1 thread; do
  job "sanitize ($san)" "CXX=g++-13 SAN=$san COMPILE_CHECKS=0 tests/run_tests.sh"
done
job gcc-latest "podman run --rm -v \"\$PWD\":/src -w /src $GCC_IMAGE sh -c 'CXX=g++ tests/run_tests.sh'"
job clang-latest 'CXX=clang++-23 tests/run_tests.sh'

# The installed package, for the package and mathopt jobs (CI's package is a step of stub, g++-13).
job install "cmake -S . -B '$work/build' -DCMAKE_INSTALL_PREFIX='$work/prefix' && cmake --install '$work/build'"
for cxx in g++-13 clang++-18; do
  job "mathopt ($cxx)" "CXX=$cxx tests/run_tests.sh --mathopt \"\$ORTOOLS_DIR\" &&
    cmake -S tests/cmake_consumer_mathopt -B '$work/consumer-mathopt-$cxx' -DCMAKE_CXX_COMPILER=$cxx \
          -DCMAKE_PREFIX_PATH=\"$work/prefix;\$ORTOOLS_DIR\" &&
    cmake --build '$work/consumer-mathopt-$cxx' -j\"\$(nproc)\" && '$work/consumer-mathopt-$cxx/lot_sizing'"
done
for cxx in g++-13 clang++-18; do
  job "package ($cxx)" "cmake -S tests/cmake_consumer -B '$work/consumer-$cxx' -DCMAKE_CXX_COMPILER=$cxx \
          -DCMAKE_PREFIX_PATH='$work/prefix' &&
    cmake --build '$work/consumer-$cxx' && '$work/consumer-$cxx/consumer'"
done

[ -z "$failed" ] || exit 1
echo "ci_local: all passed"
