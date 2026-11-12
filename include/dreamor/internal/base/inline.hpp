// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Macros that force or forbid inlining, in each compiler's own spelling. The code does not write
// [[gnu::always_inline]] directly, because cl.exe warns about an attribute that it does not know, at every warning
// level (C5030). clang-cl also defines _MSC_VER, so _MSC_VER is tested last.
#pragma once

#if defined(__GNUC__) || defined(__clang__)
/// @hidden - a specifier of dreamor's own functions
#  define DREAMOR_INLINE [[gnu::always_inline]]
/// @hidden - a specifier of dreamor's own functions
#  define DREAMOR_NOINLINE [[gnu::noinline]]
#elif defined(_MSC_VER)
#  define DREAMOR_INLINE __forceinline
#  define DREAMOR_NOINLINE __declspec(noinline)
#else
#  define DREAMOR_INLINE
#  define DREAMOR_NOINLINE
#endif
