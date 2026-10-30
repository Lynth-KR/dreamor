#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Runs clang-tidy over the library, the tests, and the examples, as .clang-tidy and its
# per-directory children configure it (tests/stub, the compile_fail cases, the ported samples
# and bench/ are not checked). Library headers are reported where a test or example includes
# them, and each header is also checked on its own.
#
#   tools/tidy.sh           report; fail on any finding (what CI runs)
#   tools/tidy.sh --fix     apply clang-tidy's fixes where it has one
#
# Needs compile_commands.json: python3 tools/gen_compile_commands.py. Findings differ between
# versions, so clang-tidy is pinned. uvx runs it, with run-clang-tidy.py and
# clang-apply-replacements from the same package (installing nothing on PATH); CLANG_TIDY_RUN=
# (empty) takes all three from PATH instead (their version must still match). JOBS is how many
# files run at once (default: every core).
set -eu
cd "$(dirname "$0")/.."
CLANG_TIDY_VERSION=23.1.3
CLANG_TIDY_RUN=${CLANG_TIDY_RUN-uvx -q --from clang-tidy==$CLANG_TIDY_VERSION}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

# CLANG_TIDY_RUN is a command prefix of several words: it is expanded unquoted below.
found=$($CLANG_TIDY_RUN clang-tidy --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n 1)
if [ "$found" != "$CLANG_TIDY_VERSION" ]; then
  echo "clang-tidy $CLANG_TIDY_VERSION needed, found ${found:-none} (${CLANG_TIDY_RUN:-PATH})" >&2
  exit 1
fi
if [ ! -f compile_commands.json ]; then
  echo "compile_commands.json is missing: python3 tools/gen_compile_commands.py" >&2
  exit 1
fi

fix=
[ "${1:-}" = "--fix" ] && fix=-fix
# The files to check, as compile_commands.json names them (absolute paths).
files='/(include/|tests/(?!stub/|compile_fail/|mathopt/compile_fail/|highs/compile_fail/|cmake_consumer)|examples/(?!mathopt_samples/))'
$CLANG_TIDY_RUN run-clang-tidy.py -p . -j "$JOBS" -quiet $fix "$files"
