# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The libcxx-latest job's machine for tools/ci_local.sh: Ubuntu 24.04, as CI's runner, with the newest LLVM's Clang
# and libc++ from apt.llvm.org. Two versions of libc++ cannot be installed side by side - their -dev packages
# conflict - so the floor's libc++ 18 is the machine's own and the newest lives here. Bump LLVM_VERSION with
# clang-latest (AGENTS.md, "Compiler support"); tools/ci_local.sh builds the image again when this file changes.
FROM docker.io/library/ubuntu:24.04
ARG LLVM_VERSION=23
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      ca-certificates wget gnupg lsb-release software-properties-common \
 && wget -qO- https://apt.llvm.org/llvm.sh | bash -s -- "$LLVM_VERSION" \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      "libc++-$LLVM_VERSION-dev" "libc++abi-$LLVM_VERSION-dev" "libunwind-$LLVM_VERSION-dev" \
 && rm -rf /var/lib/apt/lists/*
ENV CXX=clang++-$LLVM_VERSION
