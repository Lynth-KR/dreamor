# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The gcc-latest job's machine for ci/local.py: the official GCC image, which has no CMake, with Debian's. CI's
# gcc-latest job installs the same in a step of its own. Bump the tag with ci.yml's (docs/dev/ci.md, "Bumping");
# ci/local.py builds the image again when this file changes.
FROM docker.io/library/gcc:16
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends cmake \
 && rm -rf /var/lib/apt/lists/*
