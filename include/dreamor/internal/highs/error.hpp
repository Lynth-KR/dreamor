// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// HighsCallError, the exception that dreamor throws when a HiGHS call fails.
#pragma once

#include <stdexcept>
#include <string>

#include "dreamor/internal/base/third_party_headers.hpp"
DREAMOR_BEGIN_THIRD_PARTY_HEADERS
#include "Highs.h"
DREAMOR_END_THIRD_PARTY_HEADERS

namespace dreamor::highs {

/// @section dreamor::highs

/// An exception that reports a call to HiGHS that returned `HighsStatus::kError`.
///
/// The exception holds the name of the `Highs` member function that failed and the status that the function
/// returned. The message that `what()` returns names both. A call that returns `HighsStatus::kWarning` does not
/// throw. Where highspy raises a plain `Exception` for a failed call, dreamor throws a `HighsCallError`.
class HighsCallError : public std::runtime_error {
 public:
  /// Constructs the exception for the `Highs` member function `call`, which returned `status`.
  ///
  /// The exception stores the pointer `call` and does not copy the string. If the string is destroyed before the
  /// exception, the behavior of `call()` is undefined.
  ///
  /// Takes: `call` must point to a string that outlives the exception, such as a string literal. `status` may be any
  /// status, though dreamor throws the exception only for `HighsStatus::kError`.
  HighsCallError(const char* call, HighsStatus status)
      : std::runtime_error{std::string{"dreamor::highs: Highs::"} + call + " returned " + highsStatusToString(status)},
        call_{call},
        status_{status} {}

  /// Returns the name of the `Highs` member function that failed.
  [[nodiscard]] const char* call() const noexcept { return call_; }

  /// Returns the status that the failed function returned.
  [[nodiscard]] HighsStatus status() const noexcept { return status_; }

 private:
  const char* call_;  // The name of the Highs member function. dreamor passes only string literals.
  HighsStatus status_;
};

}  // namespace dreamor::highs
