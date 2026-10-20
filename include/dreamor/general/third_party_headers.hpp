// SPDX-License-Identifier: Apache-2.0
// File: third_party_headers.hpp
// Silences warnings raised inside headers that are not ours, so a build with -Wall -Wextra
// (or -Werror) reports only this library's code:
//
//     DREAMOR_BEGIN_THIRD_PARTY_HEADERS
//     #include <ilconcert/iloexpression.h>
//     DREAMOR_END_THIRD_PARTY_HEADERS
//
// Clang can switch everything off; GCC needs the warnings named, so its list covers what
// older C++ headers typically trigger under C++20. Including those headers with -isystem
// does the same job from the build side.
#pragma once

// clang-format off: one _Pragma per line; clang-format would chain them into a staircase.
#if defined(__clang__)
#  define DREAMOR_BEGIN_THIRD_PARTY_HEADERS \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Weverything\"")
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
    _Pragma("GCC diagnostic ignored \"-Wpedantic\"")
#  define DREAMOR_END_THIRD_PARTY_HEADERS _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
#  define DREAMOR_BEGIN_THIRD_PARTY_HEADERS __pragma(warning(push, 0))
#  define DREAMOR_END_THIRD_PARTY_HEADERS __pragma(warning(pop))
#else
#  define DREAMOR_BEGIN_THIRD_PARTY_HEADERS
#  define DREAMOR_END_THIRD_PARTY_HEADERS
#endif
// clang-format on
