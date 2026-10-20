#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Shows the repository as a tree, without anything git ignores (.gitignore,
# .git/info/exclude, the global excludes file): the tracked files plus the
# untracked ones git would offer to add - git's own rules, so negations like
# !/.vscode/settings.json come out exactly as git sees them.
#
#   tools/tree.sh              the whole repository
#   tools/tree.sh -L 2         any tree option works: depth 2
#   tools/tree.sh -d           directories only
set -e
cd "$(git rev-parse --show-toplevel)"
command -v tree >/dev/null 2>&1 || { echo "tree is not installed (sudo apt install tree)" >&2; exit 1; }
# A tracked file deleted from the working tree is still in the index; list only what exists.
git ls-files --cached --others --exclude-standard |
  while IFS= read -r path; do [ -e "$path" ] && printf '%s\n' "$path"; done |
  tree -a --fromfile "$@" .
