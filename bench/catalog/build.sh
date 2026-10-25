#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Builds ./catalog against the dreamor headers in ../../include and the lean Concert stub in
# ../common/stub. CXX picks the compiler (default g++; GCC 13+ or Clang 18+).
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
$CXX -std=c++20 -O2 -DNDEBUG -march=native -Wall -Wextra -I../../include -I../common/stub catalog.cpp -o catalog
echo "built bench/catalog/catalog"
