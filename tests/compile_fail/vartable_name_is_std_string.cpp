// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "prelude.hpp"
int main() {
  IloEnv env;
  const std::string name = "x";
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ(), 0, 1, ILOFLOAT, name};
}
