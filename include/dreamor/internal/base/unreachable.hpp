// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// C++23's std::unreachable for C++20. A release build reaches it where a debug build would stop on a broken contract.
#pragma once

/// @section Debug-build checks
///
/// This section lists dreamor's contracts. A contract is a precondition of an entry. If a program breaks a contract,
/// the behavior is undefined, as when a program breaks a precondition of the standard library.
///
/// A debug build checks the contracts. A debug build is one in which `NDEBUG` is not defined. If a check finds a
/// broken contract, it writes an error line that describes the contract to the calling thread's log target, as the
/// Logger section describes. Then it aborts the program. A release build does not check the contracts, as with the
/// assertions of libstdc++.
///
/// For each contract, the section shows its whole text, the entries that name it, the line that its check writes,
/// and the test cases that break it. An entry shows only the first sentence of a contract, which states the contract
/// whole, with a link to this section.

namespace dreamor::internal::inline base {

[[noreturn]] inline void unreachable() {
#if defined(__GNUC__) || defined(__clang__)
  __builtin_unreachable();
#elif defined(_MSC_VER)
  __assume(false);
#endif
}

}  // namespace dreamor::internal::inline base
