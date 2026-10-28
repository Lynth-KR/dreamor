// SPDX-License-Identifier: Apache-2.0
// What every contract_fail case shares: a few columns over plain values and entities.
#pragma once
#include <cstddef>
#include <tuple>
#include <vector>

#include "dreamor/index_set.hpp"

#ifdef __linux__
#  include <sys/prctl.h>

// Every case stops on purpose: keep the kernel from dumping its core, which a desktop session's crash
// reporter (systemd-coredump, then DrKonqi or apport) would announce as a crash.
[[maybe_unused]] inline const bool kNoCoreDump = prctl(PR_SET_DUMPABLE, 0) == 0;
#endif

struct Entity {
  std::size_t id;
};
inline std::size_t keyName(const Entity* e) {
  return e->id;
}
inline std::size_t primaryKey(const Entity& e) {
  return e.id;
}

using ICol = dreamor::Col<std::size_t, "i">;
using JCol = dreamor::Col<std::size_t, "j">;
using ECol = dreamor::Col<const Entity*, "e">;
