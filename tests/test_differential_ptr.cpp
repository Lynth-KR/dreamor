// SPDX-License-Identifier: Apache-2.0
// The differential test (differential.hpp) on entity columns, pointers into a fixed array: rows
// of 24 bytes, joined rows of four pointers, 32. Variables are named by the entity's keyName().
#include "check.hpp"
#include "differential.hpp"
#include "dreamor/index_set.hpp"

namespace {

struct Columns {
  using A = dreamor::Col<const differential::Entity*, "a">;
  using B = dreamor::Col<const differential::Entity*, "b">;
  using C = dreamor::Col<const differential::Entity*, "c">;
  using D = dreamor::Col<const differential::Entity*, "d">;
};

}  // namespace

int main() try { return differential::run<Columns>("differential_ptr"); } catch (...) {
  return ::uncaught();
}
