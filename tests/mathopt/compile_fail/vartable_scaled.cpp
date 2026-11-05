// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: Scale the sum, not the table
#include "prelude.hpp"
int main() {
  math_opt::Model model;
  const dreamor::mathopt::VarTable<cI, cJ> x{model, IJ(), 0, 1};
  (void)dreamor::mathopt::sumExpr(IJ(), x * 100).build();
}
