// SPDX-License-Identifier: Apache-2.0
// What every contract_fail case shares: a few columns over plain values and entities.
#pragma once
#include <cstddef>
#include <tuple>
#include <vector>

#include "dreamor/index_set.hpp"

struct Entity {
  std::size_t id;
};
inline std::size_t keyName(const Entity* e) {
  return e->id;
}

using ICol = dreamor::Col<std::size_t, "i">;
using JCol = dreamor::Col<std::size_t, "j">;
using ECol = dreamor::Col<const Entity*, "e">;
