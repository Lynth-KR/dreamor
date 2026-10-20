// SPDX-License-Identifier: Apache-2.0
// Minimal test helpers: no framework, so the tests build anywhere the library does.
// Prefixed: a bare CHECK collides with Abseil's (OR-Tools) and glog's.
#pragma once
#include <cstddef>
#include <cstdio>

inline std::size_t g_failures = 0;
inline std::size_t g_checks   = 0;

#define DREAMOR_CHECK(...)                                                                 \
  do {                                                                                     \
    ++g_checks;                                                                            \
    if (!(__VA_ARGS__)) {                                                                  \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
      ++g_failures;                                                                        \
    }                                                                                      \
  } while (0)

#define DREAMOR_CHECK_THROWS(Exception, ...)                                                               \
  do {                                                                                                     \
    ++g_checks;                                                                                            \
    bool thrown_ = false;                                                                                  \
    try {                                                                                                  \
      (void)(__VA_ARGS__);                                                                                 \
    } catch (const Exception&) {                                                                           \
      thrown_ = true;                                                                                      \
    }                                                                                                      \
    if (!thrown_) {                                                                                        \
      std::fprintf(stderr, "%s:%d: expected %s from: %s\n", __FILE__, __LINE__, #Exception, #__VA_ARGS__); \
      ++g_failures;                                                                                        \
    }                                                                                                      \
  } while (0)

inline int report(const char* name) {
  std::printf("%-16s %s  (%zu checks, %zu failed)\n", name, g_failures ? "FAIL" : "ok", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
