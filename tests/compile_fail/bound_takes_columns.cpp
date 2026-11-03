// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: Take the columns themselves
#include "prelude.hpp"
int main() {
  IloEnv env;
  dreamor::concert::VarTable<cI, cJ> x{env, IJ(), 0, [](std::size_t i) { return i; }};
}
