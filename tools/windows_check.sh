#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The Windows check, from WSL: builds the default-mode tests (tests/test_*.cpp against the Concert stub) with
# Visual Studio's cl.exe or clang-cl, runs each of them RUNS times, and reports what failed and every warning
# dreamor's own headers give. Windows is not on CI; AGENTS.md ("Platforms", "Checking") says when to run this.
#
#   tools/windows_check.sh [cl|clang-cl] [test_name ...]   # default cl, every test
#   RUNS=20 tools/windows_check.sh                         # runs per test (default 5): some failures are not every run
#   OPT="/O2 /DNDEBUG" tools/windows_check.sh              # optimization (default /O1, debug checks on)
#   JOBS=4 tools/windows_check.sh                          # parallel compiles (default 6)
#
# Needs Visual Studio 2022 (vswhere finds it; clang-cl is its "C++ Clang tools for Windows" component) and WSL's
# interop. The sources are copied to %TEMP%\dreamor-windows-check: cl.exe reads a \\wsl.localhost path slowly.
#
# Built so that what went wrong once cannot again: the compiles run in parallel, the tests one at a time in a
# single cmd.exe (tests run in parallel from WSL left empty logs); each result goes to a file of its own (lines
# appended to one shared file on /mnt/c got mixed); an exit status is read on a line of its own (in
# `cmd /c "x & echo %errorlevel%"` the variable expands before x runs), and written with the redirection first
# (`echo 0> f` would redirect handle 0).
set -euo pipefail

COMPILER=cl
if [ $# -gt 0 ] && { [ "$1" = cl ] || [ "$1" = clang-cl ]; }; then
  COMPILER=$1
  shift
fi
RUNS=${RUNS:-5}
OPT=${OPT:-/O1}
JOBS=${JOBS:-6}

ROOT=$(cd "$(dirname "$0")/.." && pwd)
command -v cmd.exe > /dev/null || { echo "windows_check.sh: no cmd.exe - run it from WSL"; exit 2; }
cd /mnt/c # cmd.exe complains about a working directory it cannot see

VSWHERE="/mnt/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
[ -x "$VSWHERE" ] || { echo "windows_check.sh: no vswhere.exe - is Visual Studio installed?"; exit 2; }
VS=$("$VSWHERE" -latest -products '*' -property installationPath | tr -d '\r')
[ -n "$VS" ] || { echo "windows_check.sh: vswhere found no Visual Studio"; exit 2; }
VCVARS="$VS\\VC\\Auxiliary\\Build\\vcvars64.bat"

WIN_DIR="$(cmd.exe /c 'echo %TEMP%' 2> /dev/null | tr -d '\r')\\dreamor-windows-check"
DIR=$(wslpath -u "$WIN_DIR")
rm -rf "$DIR"
mkdir -p "$DIR/out"
cp -r "$ROOT/include" "$ROOT/examples" "$ROOT/tests" "$DIR/"

if [ $# -gt 0 ]; then
  TESTS="$*"
else
  TESTS=$(cd "$ROOT/tests" && ls test_*.cpp | sed 's/\.cpp$//')
fi

case $COMPILER in
  cl) FLAGS="/nologo /std:c++20 /EHsc /permissive- /W4 $OPT" ;;
  clang-cl) FLAGS="/nologo /std:c++20 /EHsc /W4 $OPT" ;;
esac

# A .bat is CRLF; printf writes each line with its \r.
bat() { printf '%s\r\n' "$@"; }

bat '@echo off' \
  "call \"$VCVARS\" > nul || exit /b 90" \
  'cd /d %~dp0tests' \
  "$COMPILER $FLAGS /I..\\include /I..\\examples /I. /Istub %1.cpp /Fe:..\\out\\%1.exe /Fo:..\\out\\%1.obj > ..\\out\\%1.build.log 2>&1" \
  '> ..\out\%1.build.rc echo %errorlevel%' > "$DIR/build.bat"

bat '@echo off' \
  'cd /d %~dp0out' \
  'for %%t in (%*) do (' \
  "  for /l %%r in (1,1,$RUNS) do (" \
  '    if exist %%t.exe (' \
  '      %%t.exe > %%t.run%%r.log 2>&1' \
  '      > %%t.run%%r.rc call echo %%errorlevel%%' \
  '    )' \
  '  )' \
  ')' > "$DIR/run.bat"

echo "windows_check: $COMPILER $FLAGS, $(echo "$TESTS" | wc -w) tests, $RUNS runs each"
cd "$DIR" # a Windows directory: cmd.exe starts there, and the .bat names need no path
echo "$TESTS" | tr ' ' '\n' | grep . | xargs -P "$JOBS" -I{} cmd.exe /c build.bat {} > /dev/null 2>&1 || true
# shellcheck disable=SC2086 # the test names are words
cmd.exe /c run.bat $TESTS > /dev/null 2>&1 || true

status=0
for t in $TESTS; do
  build_rc=$(tr -d '\r ' < "$DIR/out/$t.build.rc" 2> /dev/null || echo missing)
  if [ "$build_rc" != 0 ]; then
    echo "$t: BUILD FAILED ($build_rc)"
    tr -d '\r' < "$DIR/out/$t.build.log" | grep -aE 'error' | head -5
    status=1
    continue
  fi
  failed=0
  last=""
  for r in $(seq 1 "$RUNS"); do
    rc=$(tr -d '\r ' < "$DIR/out/$t.run$r.rc" 2> /dev/null || echo missing)
    if [ "$rc" != 0 ]; then
      failed=$((failed + 1))
      last="run $r exit $rc"
    fi
  done
  summary=$(tr -d '\r' < "$DIR/out/$t.run1.log" | tail -n 1)
  if [ "$failed" = 0 ]; then
    echo "$t: ok, $RUNS/$RUNS runs  ($summary)"
  else
    echo "$t: FAILED $failed of $RUNS runs ($last)"
    status=1
  fi
done

# dreamor's own warnings: those whose file is under include/ (a test's own code or the STL's are not the
# headers' to fix). The messages are in Visual Studio's language, and its code page (grep -a: CP949 looks binary to
# grep, which then prints no line); the codes and places are what matter.
warnings=$(cat "$DIR"/out/*.build.log | tr -d '\r' | LC_ALL=C grep -aE '\.\.[\\/]include[\\/].*warning' |
  LC_ALL=C sed -E 's/^.*\.\.[\\/](include[^(]*)\(([0-9]+)[^)]*\).*(warning [A-Za-z]*[0-9]+).*$/\1:\2: \3/' | tr '\\' '/' |
  sort | uniq -c | sort -rn || true)
if [ -n "$warnings" ]; then
  echo "warnings from include/ (count, place, code):"
  echo "$warnings"
  status=1
else
  echo "no warnings from include/"
fi
echo "logs: $WIN_DIR\\out"
exit $status
