// SPDX-License-Identifier: Apache-2.0
// Minimal test helpers: no framework, so the tests build anywhere the library does.
// Prefixed: a bare CHECK collides with Abseil's (OR-Tools) and glog's.
#pragma once
#include <cstddef>
#include <cstdio>
#include <exception>

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables): the counters every check bumps
inline std::size_t g_failures = 0;
inline std::size_t g_checks   = 0;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

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
    } catch (const Exception&) { thrown_ = true; }                                                         \
    if (!thrown_) {                                                                                        \
      std::fprintf(stderr, "%s:%d: expected %s from: %s\n", __FILE__, __LINE__, #Exception, #__VA_ARGS__); \
      ++g_failures;                                                                                        \
    }                                                                                                      \
  } while (0)

// main's function-try-block ends in `catch (...) { return ::uncaught(); }`: an exception that
// escapes a test is reported as a failure instead of ending the program unexplained.
inline int uncaught() noexcept {
  try {
    throw;
  } catch (const std::exception& e) { std::fprintf(stderr, "uncaught exception: %s\n", e.what()); } catch (...) {
    std::fprintf(stderr, "uncaught exception, not a std::exception\n");
  }
  return 1;
}

inline int report(const char* name) {
  std::printf("%-16s %s  (%zu checks, %zu failed)\n", name, g_failures != 0 ? "FAIL" : "ok", g_checks, g_failures);
  return g_failures != 0 ? 1 : 0;
}
