// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The differential test (differential.hpp) on entity columns, Refs into a frozen EntityVector: rows
// of 24 bytes, joined rows of four Refs, 32. Variables are named by the entity's primaryKey().
#include "differential.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

struct Columns {
  DREAMOR_ENTITY_COLUMN(A, differential::Entity);
  DREAMOR_ENTITY_COLUMN(B, differential::Entity);
  DREAMOR_ENTITY_COLUMN(C, differential::Entity);
  DREAMOR_ENTITY_COLUMN(D, differential::Entity);
};

}  // namespace

int main() try { return differential::run<Columns>("differential_ref"); } catch (...) {
  return ::uncaught();
}
