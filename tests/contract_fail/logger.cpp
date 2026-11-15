// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that breaks a contract, for a debug build to stop; a case's form is docs/dev/diagnostics.md's.
#include "dreamor/logger.hpp"
#include "prelude.hpp"

#ifdef DREAMOR_CASE_time_format_ten_digits
// expect: setTimeFormat takes 10 digits - a broken contract: the digits are from 0 to 9
// contract: logger_time_digits
int main() {
  dreamor::logger::setTimeFormat(dreamor::logger::TimeUnit::kSeconds, 10);  // one past 9
  DREAMOR_LOG_INFO("not reached");
  return 0;
}
#endif  // DREAMOR_CASE_time_format_ten_digits
