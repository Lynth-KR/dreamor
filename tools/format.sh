#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Formats the code, and puts in or checks what every file carries:
# - every C++ file through clang-format, and every Python file through ruff.
# - the SPDX license line and MPL's notice at the top of every code file, and no line that is not ASCII, through
#   tools/code_files.py.
# - a hyphen for every em dash and minus sign in the Markdown files, through tools/dashes.py.
# - the copyright and the license of every file, through reuse.
# "Every" is what git tracks plus what it would add. A new file counts before it is committed, and an ignored file
# does not.
#
#   tools/format.sh           format in place (and apply ruff's safe lint fixes)
#   tools/format.sh --check   change nothing; fail if anything is not formatted (what CI runs)
#
# Formatting differs between versions, so tools/versions.sh pins both formatters. uvx runs the pinned versions and
# installs nothing on PATH. CLANG_FORMAT and RUFF override the commands: CLANG_FORMAT=clang-format, for example, runs
# the clang-format on PATH. Its version must still be the pinned one. This script checks clang-format's version, and
# ruff checks its own against ruff.toml.
set -eu
cd "$(dirname "$0")/.."
. tools/versions.sh
CLANG_FORMAT=${CLANG_FORMAT:-uvx -q clang-format@$CLANG_FORMAT_VERSION}
RUFF=${RUFF:-uvx -q ruff@$RUFF_VERSION}
REUSE_VERSION=6.3.0
REUSE=${REUSE:-uvx -q reuse@$REUSE_VERSION}
CHECK=
[ "${1:-}" = "--check" ] && CHECK=1

# CLANG_FORMAT and RUFF are commands of several words: they are expanded unquoted below.
if ! $CLANG_FORMAT --version | grep -qwF "version $CLANG_FORMAT_VERSION"; then
  echo "clang-format $CLANG_FORMAT_VERSION needed; $CLANG_FORMAT is:" >&2
  $CLANG_FORMAT --version >&2
  exit 1
fi

# files [-z] PATHSPEC...: tracked and not-yet-added files, minus what git ignores.
files() { git ls-files --cached --others --exclude-standard "$@"; }

python3 tools/code_files.py ${CHECK:+--check}

# Every file says its copyright and license, in itself or in REUSE.toml (README.md, "License"). Nothing fixes a gap
# but a person, so both modes check.
if ! $REUSE lint --quiet; then
  $REUSE lint --lines >&2
  exit 1
fi

if [ -n "$CHECK" ]; then
  files -z '*.hpp' '*.cpp' '*.cc' '*.h' | xargs -0 $CLANG_FORMAT --dry-run --Werror
  files -z '*.py' | xargs -0 $RUFF format --check
  files -z '*.py' | xargs -0 $RUFF check
  python3 tools/dashes.py --check
else
  files -z '*.hpp' '*.cpp' '*.cc' '*.h' | xargs -0 $CLANG_FORMAT -i
  files -z '*.py' | xargs -0 $RUFF format
  files -z '*.py' | xargs -0 $RUFF check --fix
  python3 tools/dashes.py
fi
