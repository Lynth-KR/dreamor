// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// File: inline.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// Inlining forced or forbidden, as fmt's FMT_INLINE and FMT_NOINLINE: each compiler spells it its own way,
// and cl.exe warns about an attribute it does not know (C5030, at every warning level), so gnu:: cannot be
// written bare. Each is a specifier of its own, before the declaration: [[nodiscard]] DREAMOR_INLINE int f();
// clang-cl defines _MSC_VER too, so __clang__ and __GNUC__ are tested first.
#pragma once

#if defined(__GNUC__) || defined(__clang__)
#  define DREAMOR_INLINE [[gnu::always_inline]]
#  define DREAMOR_NOINLINE [[gnu::noinline]]
#elif defined(_MSC_VER)
#  define DREAMOR_INLINE __forceinline
#  define DREAMOR_NOINLINE __declspec(noinline)
#else
#  define DREAMOR_INLINE
#  define DREAMOR_NOINLINE
#endif
