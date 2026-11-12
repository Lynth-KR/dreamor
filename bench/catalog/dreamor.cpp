// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/dreamor.cpp - the main of the dreamor arm, which build.sh builds as ./catalog, and as ./catalog8 for k = 8.
#include <string>

#include "../common/result.hpp"
#include "dreamor/arm.hpp"
#include "dreamor/entities.hpp"
#include "dreamor/group.hpp"
#include "dreamor/lookup.hpp"
#include "dreamor/make.hpp"
#include "dreamor/order.hpp"
#include "dreamor/setop.hpp"
#include "dreamor/slice.hpp"
#include "driver.hpp"

namespace {

void runFamily(const catalog::Args& a, const Data& d, const Samples& s, Result& r) {
  if (a.family == "lookup") {
    lookup::run(a.impl, d, s, r);
  } else if (a.family == "group") {
    group::run(a.impl, d, s, r);
  } else if (a.family == "setop") {
    setop::run(a.impl, d, s, r);
  } else if (a.family == "slice") {
    slice::run(a.impl, d, a.cell.seed, r);
  } else if (a.family == "make") {
    make::run(a.impl, d, s, r);
  } else if (a.family == "order") {
    order::run(a.impl, d, s, r);
  } else {
    entities::run(a.impl, a.key, a.access, a.cell.n_target, a.cell.seed, r);
  }
}

}  // namespace

int main(int argc, char** argv) {
  catalog::runCell<DreamorArm>(argc, argv, runFamily);
}
