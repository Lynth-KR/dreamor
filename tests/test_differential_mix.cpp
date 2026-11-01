// SPDX-License-Identifier: Apache-2.0
// The differential test (differential.hpp) on 4- and 8-byte columns in turn: rows with padding
// between their columns, aligned to 8.
#include <cstddef>
#include <cstdint>

#include "check.hpp"
#include "differential.hpp"
#include "dreamor/index_set.hpp"

namespace {

struct Columns {
  using A = dreamor::Col<std::uint32_t, "a">;
  using B = dreamor::Col<std::size_t, "b">;
  using C = dreamor::Col<std::uint32_t, "c">;
  using D = dreamor::Col<std::size_t, "d">;
};

}  // namespace

int main() try { return differential::run<Columns>("differential_mix"); } catch (...) {
  return ::uncaught();
}
