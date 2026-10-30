// SPDX-License-Identifier: Apache-2.0
// expect: A ValueTable is built on a set with exactly its key columns
#include "prelude.hpp"
int main() {
  const dreamor::ValueTable<JCol, ICol> x{IJ(), 1.0};
}
