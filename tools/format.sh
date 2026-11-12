#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# REUSE-IgnoreStart
# Formats every C++ file with clang-format and every Python file with ruff, and puts the SPDX
# license line at the top of every code file (after a #! line, if any), and MPL's Exhibit A notice
# under it, set off by a bare comment line from a comment that follows; a code file that is not ASCII
# fails, the lines named. A Markdown file's em dashes and minus signs become hyphens (tools/dashes.py). "Every" is
# what git tracks plus
# what it would add - a new file counts before it is committed; ignored ones do not. Google's samples in
# examples/mathopt_samples/ keep their own Apache-2.0 line and header.
#
#   tools/format.sh           format in place (and apply ruff's safe lint fixes)
#   tools/format.sh --check   change nothing; fail if anything is not formatted (what CI runs)
#
# Formatting differs between versions, so both are pinned: clang-format here, ruff in ruff.toml.
# uvx runs the pinned versions (installing nothing on PATH); CLANG_FORMAT and RUFF override the
# commands, e.g. CLANG_FORMAT=clang-format for one on PATH (its version must still match).
set -eu
cd "$(dirname "$0")/.."
CLANG_FORMAT_VERSION=23.1.3
RUFF_VERSION=$(sed -n 's/^required-version = "==\(.*\)"/\1/p' ruff.toml)
CLANG_FORMAT=${CLANG_FORMAT:-uvx -q clang-format@$CLANG_FORMAT_VERSION}
RUFF=${RUFF:-uvx -q ruff@$RUFF_VERSION}
MPL="SPDX-License-Identifier: MPL-2.0"
MIT0="SPDX-License-Identifier: MIT-0"
REUSE_VERSION=6.3.0
REUSE=${REUSE:-uvx -q reuse@$REUSE_VERSION}
NOTICE_1="This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was"
NOTICE_2="not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/."
CHECK=
[ "${1:-}" = "--check" ] && CHECK=1

# CLANG_FORMAT and RUFF are commands of several words: they are expanded unquoted below.
found=$($CLANG_FORMAT --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n 1)
if [ "$found" != "$CLANG_FORMAT_VERSION" ]; then
  echo "clang-format $CLANG_FORMAT_VERSION needed, found ${found:-none} ($CLANG_FORMAT)" >&2
  exit 1
fi

# files [-z] PATHSPEC...: tracked and not-yet-added files, minus what git ignores.
files() { git ls-files --cached --others --exclude-standard "$@"; }

# spdx PREFIX LINE PATHSPEC...: adds "PREFIX LINE" where it is missing; with --check, names those files.
spdx() {
  line="$1 $2"
  shift 2
  files "$@" | while IFS= read -r f; do
    head -n 2 "$f" | grep -qxF "$line" && continue
    if [ -n "$CHECK" ]; then
      echo "$f: missing \"$line\""
    else
      awk -v line="$line" 'NR == 1 && /^#!/ { print; print line; next } NR == 1 { print line } { print }' "$f" > "$f.spdx"
      cat "$f.spdx" > "$f" && rm "$f.spdx"  # cat keeps the file's mode
    fi
  done
}
# notice PREFIX PATHSPEC...: puts MPL's Exhibit A notice right under the SPDX line, and a bare PREFIX line
# between it and a comment that follows, where they are not so; with --check, names those files. In every
# code file, as Eigen does, so that the notice travels with a file copied on its own.
notice() {
  prefix=$1
  shift
  files "$@" | while IFS= read -r f; do
    awk -v spdx="$prefix $MPL" -v a="$prefix $NOTICE_1" -v b="$prefix $NOTICE_2" -v p="$prefix" '
      state == 0 && NR <= 2 && $0 == spdx { print; print a; print b; state = 1; next }
      state == 1 && ($0 == a || $0 == b || $0 == p) { next }
      state == 1 { if (index($0, p " ") == 1) print p; state = 2 }
      { print }' "$f" > "$f.notice"
    if cmp -s "$f" "$f.notice"; then
      rm "$f.notice"
    elif [ -n "$CHECK" ]; then
      rm "$f.notice"
      echo "$f: MPL's Exhibit A notice is not right under the SPDX line"
    else
      cat "$f.notice" > "$f" && rm "$f.notice"  # cat keeps the file's mode
    fi
  done
}
# ascii PATHSPEC...: names every line that is not ASCII, in either mode - nothing can fix it but a person. cl.exe
# reads a file without a byte-order mark in the system's code page, so one UTF-8 character in a header is a
# warning in every Windows build that includes it (docs/dev/platforms.md).
ascii() {
  files -z "$@" | xargs -0 env LC_ALL=C grep -n "$(printf '[\200-\377]')" | sed 's/$/  <- not ASCII/' || true
}
samples=':!examples/mathopt_samples/*.cc'
# The code that the documents show and users copy is MIT-0 (README.md, "License"): the examples but Google's samples,
# and the guide's code. Everything else is MPL-2.0.
mit0='examples/*.hpp examples/*.cpp tests/docs/guide/*.cpp tests/docs/guide/*.hpp tests/docs/highs/*.cpp tests/docs/highs/*.hpp'
not_mit0=':!examples/*.hpp :!examples/*.cpp :!tests/docs/guide/* :!tests/docs/highs/*'
scripts='tools/clang-format ci/hooks/pre-push'  # shell scripts without an extension
# $scripts is a list of paths: expanded unquoted below.
missing=$(
  spdx // "$MPL" '*.hpp' '*.cpp' '*.cc' '*.h' "$samples" $not_mit0
  spdx // "$MIT0" $mit0
  spdx '#' "$MPL" '*.py' '*.sh' '*CMakeLists.txt' '*.cmake' '*.cmake.in' '*.Containerfile' $scripts
  notice // '*.hpp' '*.cpp' '*.cc' '*.h' "$samples" $not_mit0
  notice '#' '*.py' '*.sh' '*CMakeLists.txt' '*.cmake' '*.cmake.in' '*.Containerfile' $scripts
  ascii '*.hpp' '*.cpp' '*.cc' '*.h' '*.py' '*.sh' '*CMakeLists.txt' '*.cmake' '*.cmake.in' '*.Containerfile' $scripts
)
if [ -n "$missing" ]; then
  echo "$missing" >&2
  exit 1
fi
# REUSE-IgnoreEnd

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
