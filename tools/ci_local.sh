#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Runs what CI runs (.github/workflows/ci.yml) on this machine, over one commit checked out
# apart: what a push would publish, not the working tree. It needs every tool CI installs and
# stops before running anything if one is missing - a partial run is not a pass.
#
#   tools/ci_local.sh         HEAD
#   tools/ci_local.sh REV     another commit
#
# A pass is recorded by the commit's tree - the content it tested, whatever the message or the
# signature - in ci-passed/ under the repository's git directory, where tools/hooks/pre-push looks
# for it before a push to main.
#
# ORTOOLS_DIR is the OR-Tools C++ prebuilt CI downloads (v9.15; default /opt/ortools). Format and
# tidy run first, and nothing else if either fails, as on CI; then the rest run CI_JOBS at a time
# (default 4), as CI's runners do, each in a checkout of its own - tests/run_tests.sh builds in the
# tree - and each compiling JOBS at a time (default: half the cores). Logs go to a temporary folder,
# kept if a job fails.
# The jobs are written down twice: change this together with ci.yml.
set -eu
cd "$(dirname "$0")/.."
rev=$(git rev-parse --verify "${1:-HEAD}^{commit}")
ORTOOLS_DIR=${ORTOOLS_DIR:-/opt/ortools}
GCC_IMAGE=docker.io/library/gcc:16
EMSDK_IMAGE=docker.io/emscripten/emsdk:6.0.13
# Images this script builds for itself, from the commit's tools/containers/, each named by the content of the files
# it is built from, so a change to one of them builds it again: the newest LLVM's libc++, which cannot share a
# machine with the floor's, and HiGHS built by the wasm job's Emscripten.
image_name() {
  name=$1
  shift
  printf 'localhost/dreamor-%s:%s' "$name" \
    "$(for f in "$@"; do git rev-parse "$rev:tools/containers/$f"; done | sha256sum | cut -c1-12)"
}
LIBCXX_IMAGE=$(image_name libcxx-latest libcxx-latest.Containerfile)
HIGHS_WASM_IMAGE=$(image_name highs-wasm highs-wasm.Containerfile build-highs-wasm.sh)

# The machine: every tool CI installs. uv runs the pinned formatters and clang-tidy (uvx), at the
# versions tools/format.sh, tools/tidy.sh and ruff.toml pin.
missing=
lack() { missing="$missing  $1\n"; }
for tool in g++-13 clang++-18 clang++-23 cmake podman python3 uv; do
  command -v "$tool" > /dev/null 2>&1 || lack "$tool"
done
for image in "$GCC_IMAGE" "$EMSDK_IMAGE"; do
  if command -v podman > /dev/null 2>&1 && ! podman image exists "$image"; then
    lack "the $image image: podman pull $image"
  fi
done
[ -f "$ORTOOLS_DIR/include/ortools/math_opt/cpp/math_opt.h" ] || lack "OR-Tools at ORTOOLS_DIR ($ORTOOLS_DIR)"
[ -f /usr/lib/llvm-18/include/c++/v1/version ] ||
  lack "libc++ 18: sudo apt install libc++-18-dev libc++abi-18-dev libunwind-18-dev"
if [ -n "$missing" ]; then
  printf 'ci_local: this machine lacks what CI has:\n%b' "$missing" >&2
  exit 1
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/dreamor-ci.XXXXXX")
logs=$work/logs
mkdir "$logs"
failed=
finish() {
  for dir in "$work"/src*; do
    [ -d "$dir" ] && git worktree remove --force "$dir" 2> /dev/null || true
  done
  if [ -n "$failed" ]; then
    echo "ci_local: FAILED:$failed - logs in $logs"
  else
    rm -rf "$work"
  fi
}
trap finish EXIT
trap 'failed=" interrupted"; exit 130' INT TERM

echo "ci_local: $(git log -1 --format='%h %s' "$rev")"
# build_image NAME CONTAINERFILE: builds the image NAME from the commit's tools/containers/ unless it exists.
build_image() {
  podman image exists "$1" && return 0
  if [ ! -d "$work/containers" ]; then
    mkdir "$work/containers"
    git archive "$rev" tools/containers | tar -x -C "$work/containers"
  fi
  echo "ci_local: building $1 (once per change of what it is built from; minutes)"
  podman build -q -t "$1" -f "$work/containers/tools/containers/$2" "$work/containers/tools/containers" > /dev/null
}
build_image "$LIBCXX_IMAGE" libcxx-latest.Containerfile
build_image "$HIGHS_WASM_IMAGE" highs-wasm.Containerfile
CI_JOBS=${CI_JOBS:-4}
JOBS=${JOBS:-$(($(nproc) / 2))}
export ORTOOLS_DIR JOBS
count=0
running=""
# job NAME COMMAND: runs COMMAND in a checkout of its own, in the background, at most CI_JOBS at
# once; its output in NAME.log, and its line of the summary in <n>.result.
job() {
  count=$((count + 1))
  dir=$work/src$count
  git worktree add -q --detach "$dir" "$rev"
  (
    start=$(date +%s)
    if (cd "$dir" && sh -c "$2") > "$logs/$1.log" 2>&1 < /dev/null; then
      verdict=ok
    else
      verdict=FAILED
    fi
    printf '%s\t%s\t%s\n' "$verdict" "$1" "$(($(date +%s) - start))" > "$logs/$count.result"
  ) &
  running="$running $!"
  # shellcheck disable=SC2086
  set -- $running
  if [ $# -ge "$CI_JOBS" ]; then
    wait "$1" || true
    shift
    running="$*"
  fi
}
# Waits for every job started, then prints their lines in the order they were started.
report() {
  for pid in $running; do wait "$pid" || true; done
  running=""
  for n in $(seq "${reported:-1}" "$count"); do
    IFS="$(printf '\t')" read -r verdict name took < "$logs/$n.result"
    printf '  %-32s %-7s %4ss\n' "$name" "$verdict" "$took"
    [ "$verdict" = ok ] || failed="$failed $name"
  done
  reported=$((count + 1))
}

job format 'tools/format.sh --check'
job tidy 'python3 tools/gen_compile_commands.py && tools/tidy.sh'
report
[ -z "$failed" ] || exit 1

# The installed package, for the package and mathopt jobs (CI's package is a step of stub, g++-13).
job install "cmake -S . -B '$work/build' -DCMAKE_INSTALL_PREFIX='$work/prefix' && cmake --install '$work/build'"
report
[ -z "$failed" ] || exit 1

# GCC 13 at -O1 -g is sanitize (g++-13, 1); Clang 18 at -O1 -g runs only the compiler's checks, its other
# tests being sanitize (clang++-18, 1); the compile-time checks run once per compiler. As on CI.
job "stub (g++-13, -O3 -DNDEBUG)" "CXX=g++-13 OPT='-O3 -DNDEBUG' tests/run_tests.sh"
job "stub (clang++-18, -O1 -g)" "CXX=clang++-18 OPT='-O1 -g' UNIT_TESTS=0 tests/run_tests.sh"
job "stub (clang++-18, -O3 -DNDEBUG)" "CXX=clang++-18 OPT='-O3 -DNDEBUG' COMPILE_CHECKS=0 tests/run_tests.sh"
job "sanitize (g++-13, 1)" "CXX=g++-13 SAN=1 COMPILE_CHECKS=0 tests/run_tests.sh"
job "sanitize (clang++-18, 1)" "CXX=clang++-18 SAN=1 COMPILE_CHECKS=0 tests/run_tests.sh"
job "sanitize (g++-13, thread)" "CXX=g++-13 SAN=thread COMPILE_CHECKS=0 tests/run_tests.sh"
job gcc-latest "podman run --rm -e JOBS -v \"\$PWD\":/src -w /src $GCC_IMAGE sh -c 'CXX=g++ tests/run_tests.sh'"
job clang-latest 'CXX=clang++-23 tests/run_tests.sh'
job "libcxx (18)" "CXX=clang++-18 STDLIB=libc++ COMPILE_CHECKS=0 tests/run_tests.sh"
job "libcxx (latest)" "podman run --rm -e JOBS -v \"\$PWD\":/src -w /src $LIBCXX_IMAGE sh -c 'STDLIB=libc++ COMPILE_CHECKS=0 tests/run_tests.sh'"
job wasm "podman run --rm -e JOBS -v \"\$PWD\":/src -w /src $EMSDK_IMAGE sh -c 'WASM=1 tests/run_tests.sh'"
job "wasm (highs)" "podman run --rm -e JOBS -v \"\$PWD\":/src -w /src $HIGHS_WASM_IMAGE sh -c 'WASM=1 tests/run_tests.sh --highs /opt/highs'"
for cxx in g++-13 clang++-18; do
  job "mathopt ($cxx)" "CXX=$cxx tests/run_tests.sh --mathopt \"\$ORTOOLS_DIR\" &&
    cmake -S tests/cmake_consumer_mathopt -B '$work/consumer-mathopt-$cxx' -DCMAKE_CXX_COMPILER=$cxx \
          -DCMAKE_PREFIX_PATH=\"$work/prefix;\$ORTOOLS_DIR\" &&
    cmake --build '$work/consumer-mathopt-$cxx' -j\"\$(nproc)\" && '$work/consumer-mathopt-$cxx/lot_sizing'"
done
for cxx in g++-13 clang++-18; do
  job "highs ($cxx)" "CXX=$cxx tests/run_tests.sh --highs \"\$ORTOOLS_DIR\""
done
for cxx in g++-13 clang++-18; do
  job "package ($cxx)" "cmake -S tests/cmake_consumer -B '$work/consumer-$cxx' -DCMAKE_CXX_COMPILER=$cxx \
          -DCMAKE_PREFIX_PATH='$work/prefix' &&
    cmake --build '$work/consumer-$cxx' && '$work/consumer-$cxx/consumer'"
done
report

[ -z "$failed" ] || exit 1
passed=$(git rev-parse --git-common-dir)/ci-passed
mkdir -p "$passed"
echo "$(date '+%F %T') $(uname -n) $(git rev-parse --short "$rev")" > "$passed/$(git rev-parse "$rev^{tree}")"
echo "ci_local: all passed"
