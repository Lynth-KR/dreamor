// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
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

using cI = dreamor::Col<std::size_t, "i">;
using cJ = dreamor::Col<std::size_t, "j">;
using cE = dreamor::Col<const Entity*, "e">;
