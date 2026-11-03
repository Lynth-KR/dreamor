// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The differential test (differential.hpp) on columns of std::uint32_t: rows of 12 bytes, aligned
// to 4 only, joined rows of 16.
#include <cstdint>

#include "check.hpp"
#include "differential.hpp"
#include "dreamor/index_set.hpp"

namespace {

struct Columns {
  using A = dreamor::Col<std::uint32_t, "a">;
  using B = dreamor::Col<std::uint32_t, "b">;
  using C = dreamor::Col<std::uint32_t, "c">;
  using D = dreamor::Col<std::uint32_t, "d">;
};

}  // namespace

int main() try { return differential::run<Columns>("differential_u32"); } catch (...) {
  return ::uncaught();
}
