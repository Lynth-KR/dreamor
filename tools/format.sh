#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Formats every tracked C++ file with clang-format and every tracked Python file with ruff, and
# puts the SPDX license line at the top of every tracked code file (after a #! line, if any).
#
#   tools/format.sh           format in place (and apply ruff's safe lint fixes)
#   tools/format.sh --check   change nothing; fail if anything is not formatted (what CI runs)
#
# Formatting differs between versions, so both are pinned: clang-format here, ruff in ruff.toml.
#   uv tool install clang-format==23.1.3 && uv tool install ruff==0.17.1
# CLANG_FORMAT and RUFF override the binaries.
set -eu
cd "$(dirname "$0")/.."
CLANG_FORMAT_VERSION=23.1.3
CLANG_FORMAT=${CLANG_FORMAT:-clang-format}
RUFF=${RUFF:-ruff}
SPDX="SPDX-License-Identifier: Apache-2.0"
CHECK=
[ "${1:-}" = "--check" ] && CHECK=1

found=$("$CLANG_FORMAT" --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n 1)
if [ "$found" != "$CLANG_FORMAT_VERSION" ]; then
  echo "clang-format $CLANG_FORMAT_VERSION needed, found ${found:-none}: uv tool install clang-format==$CLANG_FORMAT_VERSION" >&2
  exit 1
fi

# spdx PREFIX PATHSPEC...: adds "PREFIX $SPDX" where it is missing; with --check, names those files.
spdx() {
  line="$1 $SPDX"
  shift
  git ls-files "$@" | while IFS= read -r f; do
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
  git ls-files -z '*.hpp' '*.cpp' '*.cc' '*.h' | xargs -0 "$CLANG_FORMAT" --dry-run --Werror
  git ls-files -z '*.py' | xargs -0 "$RUFF" format --check
  git ls-files -z '*.py' | xargs -0 "$RUFF" check
else
  git ls-files -z '*.hpp' '*.cpp' '*.cc' '*.h' | xargs -0 "$CLANG_FORMAT" -i
  git ls-files -z '*.py' | xargs -0 "$RUFF" format
  git ls-files -z '*.py' | xargs -0 "$RUFF" check --fix
fi
