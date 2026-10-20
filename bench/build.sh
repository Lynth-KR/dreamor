#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Builds ./bench against the dreamor headers in ../include and the lean Concert stub in ./stub.
# CXX picks the compiler (default g++; GCC 13+ or Clang 17+ for C++20 and std::format).
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
$CXX -std=c++20 -O2 -DNDEBUG -march=native -Wall -Wextra -I../include -Istub bench.cpp -o bench
echo "built bench/bench"
