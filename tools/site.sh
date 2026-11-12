#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The documentation site (zensical.toml), through Zensical at the version pinned here, run by uvx. With no argument
# it builds the site into site/, and fails on what would break a page: a guide page's region that its test no longer
# has, a link to a page or an anchor that is not there. `tools/site.sh serve` previews it at http://127.0.0.1:8000/, rebuilding as the files change.
set -eu
cd "$(dirname "$0")/.."

ZENSICAL_VERSION=0.1.0
ZENSICAL=${ZENSICAL:-uvx -q zensical@$ZENSICAL_VERSION}

case "${1:-build}" in
  build) exec $ZENSICAL build --clean --strict ;;
  serve) exec $ZENSICAL serve --dev-addr 127.0.0.1:8000 ;;
  *)
    echo "usage: tools/site.sh [build|serve]" >&2
    exit 2
    ;;
esac
