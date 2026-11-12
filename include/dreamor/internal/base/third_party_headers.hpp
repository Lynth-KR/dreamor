// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Turns warnings off inside a solver's headers, so that -Wall -Wextra, or -Werror, reports only dreamor's code. Clang
// can turn every warning off at once. GCC needs each warning named: these are the warnings that older C++ headers
// raise under C++20.
#pragma once

// clang-format off: each _Pragma stays on its own line, which clang-format would break into a staircase.
#ifdef __clang__
/// @hidden - for dreamor's backends, around a solver's #include
#  define DREAMOR_BEGIN_THIRD_PARTY_HEADERS \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Weverything\"")
/// @hidden - for dreamor's backends, around a solver's #include
#  define DREAMOR_END_THIRD_PARTY_HEADERS _Pragma("clang diagnostic pop")
#elif defined(__GNUC__)
#  define DREAMOR_BEGIN_THIRD_PARTY_HEADERS                                   \
    _Pragma("GCC diagnostic push")                                           \
    _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")          \
    _Pragma("GCC diagnostic ignored \"-Wdeprecated-copy\"")                  \
    _Pragma("GCC diagnostic ignored \"-Wdeprecated-enum-enum-conversion\"")  \
    _Pragma("GCC diagnostic ignored \"-Wvolatile\"")                         \
    _Pragma("GCC diagnostic ignored \"-Wclass-memaccess\"")                  \
    _Pragma("GCC diagnostic ignored \"-Wunused-parameter\"")                 \
    _Pragma("GCC diagnostic ignored \"-Wunused-variable\"")                  \
    _Pragma("GCC diagnostic ignored \"-Wsign-compare\"")                     \
    _Pragma("GCC diagnostic ignored \"-Wignored-qualifiers\"")               \
    _Pragma("GCC diagnostic ignored \"-Wimplicit-fallthrough\"")             \
    _Pragma("GCC diagnostic ignored \"-Wmisleading-indentation\"")           \
    _Pragma("GCC diagnostic ignored \"-Wcast-function-type\"")               \
    _Pragma("GCC diagnostic ignored \"-Woverloaded-virtual\"")               \
    _Pragma("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")              \
    _Pragma("GCC diagnostic ignored \"-Wpedantic\"")                         \
    _Pragma("GCC diagnostic ignored \"-Wconversion\"")                       \
    _Pragma("GCC diagnostic ignored \"-Wsign-conversion\"")                  \
    _Pragma("GCC diagnostic ignored \"-Wold-style-cast\"")                   \
    _Pragma("GCC diagnostic ignored \"-Wshadow\"")                           \
    _Pragma("GCC diagnostic ignored \"-Wfloat-equal\"")                      \
    _Pragma("GCC diagnostic ignored \"-Wuseless-cast\"")
#  define DREAMOR_END_THIRD_PARTY_HEADERS _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
#  define DREAMOR_BEGIN_THIRD_PARTY_HEADERS __pragma(warning(push, 0))
#  define DREAMOR_END_THIRD_PARTY_HEADERS __pragma(warning(pop))
#else
#  define DREAMOR_BEGIN_THIRD_PARTY_HEADERS
#  define DREAMOR_END_THIRD_PARTY_HEADERS
#endif
// clang-format on
