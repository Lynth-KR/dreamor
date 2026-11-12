// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// DREAMOR_LIFETIMEBOUND, Clang's mark that a function's result refers to one of its parameters. With it, Clang warns
// (-Wdangling, on by default) where a variable keeps such a result after the temporary that was passed as the
// argument is destroyed at the end of its statement. GCC and cl.exe have no such mark, so the macro is empty there.
// clang-cl defines __clang__ too, and gets the mark.
#pragma once

#ifdef __clang__
/// @hidden - a specifier of dreamor's own parameters
#  define DREAMOR_LIFETIMEBOUND [[clang::lifetimebound]]
#else
#  define DREAMOR_LIFETIMEBOUND
#endif
