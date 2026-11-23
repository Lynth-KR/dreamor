# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The formatters and the linter, pinned because their output differs between versions (docs/dev/code-style.md,
# "Tools"). tools/format.sh, tools/clang-format and tools/tidy.sh source this file, and uvx runs each at its pin.
# clang-format and clang-tidy move together, to the newest patch of each LLVM major. ruff.toml's required-version
# names ruff's pin as well: ruff refuses to run when the two differ.
CLANG_FORMAT_VERSION=23.1.3
CLANG_TIDY_VERSION=23.1.3
RUFF_VERSION=0.17.1
