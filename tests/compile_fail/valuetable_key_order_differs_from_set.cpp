// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: A ValueTable is built on a set with exactly its key columns
#include "prelude.hpp"
int main() {
  const dreamor::ValueTable<cJ, cI> x{IJ(), 1.0};
}
