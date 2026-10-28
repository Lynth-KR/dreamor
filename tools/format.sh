#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Formats every C++ file with clang-format and every Python file with ruff, and puts the SPDX
# license line at the top of every code file (after a #! line, if any). "Every" is what git
# tracks plus what it would add - a new file counts before it is committed; ignored ones do not.
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
SPDX="SPDX-License-Identifier: Apache-2.0"
CHECK=
[ "${1:-}" = "--check" ] && CHECK=1

# CLANG_FORMAT and RUFF are commands of several words: they are expanded unquoted below.
found=$($CLANG_FORMAT --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n 1)
if [ "$found" != "$CLANG_FORMAT_VERSION" ]; then
  echo "clang-format $CLANG_FORMAT_VERSION needed, found ${found:-none} ($CLANG_FORMAT)" >&2
  exit 1
fi

# files [-z] PATHSPEC...: tracked and not-yet-added files, minus what .gitignore excludes.
files() { git ls-files --cached --others --exclude-standard "$@"; }

# spdx PREFIX PATHSPEC...: adds "PREFIX $SPDX" where it is missing; with --check, names those files.
spdx() {
  line="$1 $SPDX"
  shift
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
missing=$(spdx // '*.hpp' '*.cpp' '*.cc' '*.h'; spdx '#' '*.py' '*.sh' '*CMakeLists.txt' '*.cmake' '*.cmake.in')
if [ -n "$missing" ]; then
  echo "$missing" >&2
  exit 1
fi

if [ -n "$CHECK" ]; then
  files -z '*.hpp' '*.cpp' '*.cc' '*.h' | xargs -0 $CLANG_FORMAT --dry-run --Werror
  files -z '*.py' | xargs -0 $RUFF format --check
  files -z '*.py' | xargs -0 $RUFF check
else
  files -z '*.hpp' '*.cpp' '*.cc' '*.h' | xargs -0 $CLANG_FORMAT -i
  files -z '*.py' | xargs -0 $RUFF format
  files -z '*.py' | xargs -0 $RUFF check --fix
fi
