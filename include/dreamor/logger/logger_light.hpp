// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <source_location>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#  include <io.h>
#  define DREAMOR_LOGGER_ISATTY(fd) _isatty(fd)
#  define DREAMOR_LOGGER_FILENO(f) _fileno(f)
#else
#  include <unistd.h>
#  define DREAMOR_LOGGER_ISATTY(fd) isatty(fd)
#  define DREAMOR_LOGGER_FILENO(f) fileno(f)
#endif

// Compile-time switch for the "[+123.456ms]" elapsed-time prefix. On by
// default; define DREAMOR_LOGGER_ENABLE_TIMESTAMP=0 before including this
// header to drop it (e.g. a downstream tool parses a fixed line format that
// doesn't expect it). Off means zero cost: elapsedMs() is never called.
#ifndef DREAMOR_LOGGER_ENABLE_TIMESTAMP
#  define DREAMOR_LOGGER_ENABLE_TIMESTAMP 1
#endif

namespace dreamor::logger::detail {

enum class Level : std::uint8_t { kInfo = 0, kWarning = 1, kError = 2 };

inline constexpr std::uint16_t kBufferSize                   = 1024;
inline constexpr std::string_view kColorReset                = "\033[0m";
inline constexpr std::string_view kColorDim                  = "\033[90m";
inline constexpr std::array<std::string_view, 3> kLevelColor = {"\033[32m", "\033[33m", "\033[31m"};
inline constexpr std::array<std::string_view, 3> kLevelTag   = {"INFO", "WARN", "ERROR"};

// Per-thread output target (default stderr). thread_local so N threads
// building N models in parallel each get their own log file with zero
// cross-talk; a spawned thread always starts at the stderr default.
inline thread_local FILE* g_output = stderr;  // NOLINT

// Cached per thread on redirect (see setOutput) rather than re-checked via
// isatty() on every log() call - that syscall showed up as a real cost.
inline thread_local bool g_output_is_tty = DREAMOR_LOGGER_ISATTY(DREAMOR_LOGGER_FILENO(stderr)) != 0;  // NOLINT

// NO_COLOR (https://no-color.org), read once (lazy static, thread-safe init)
// instead of on every call. Process-wide on purpose: it's an environment
// setting, not a per-model preference.
inline bool noColorRequested() noexcept {
  static const bool requested = [] {
    const char* no_color = std::getenv("NO_COLOR");  // NOLINT
    return no_color != nullptr && no_color[0] != '\0';
  }();
  return requested;
}

inline bool colorEnabled() noexcept {
  return !noColorRequested() && g_output_is_tty;
}

// steady_clock: durations between build phases matter here, not wall-clock
// date/time, so NTP steps must not affect it.
inline std::chrono::steady_clock::time_point startTime() noexcept {
  static const auto t0 = std::chrono::steady_clock::now();
  return t0;
}

// ms elapsed since the first log call in this process - lets two lines
// bracketing a block show how long it took, with no extra std::chrono code
// at the call site.
inline double elapsedMs() noexcept {
  const auto t0  = startTime();  // sampled first so t0 <= now always (no "-0.000ms" on line 1)
  const auto now = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(now - t0).count();
}

// Relaxed: gates whether a line prints at all, not any data it protects.
inline std::atomic<Level> g_min_level{Level::kInfo};  // NOLINT

// NOTE: no mutex here. POSIX/glibc guarantees fwrite/fflush/etc. on the SAME
// stream are already serialized against each other via the stream's own
// internal lock (see glibc's flockfile/funlockfile docs - that API exists
// specifically for combining MULTIPLE calls atomically, which implies each
// individual call already is). MSVC's CRT is the same by default (fwrite is
// locked; _fwrite_nolock is the explicit opt-out). A custom mutex here would
// just be re-locking what the platform already locks - and would serialize
// threads writing to COMPLETELY DIFFERENT files for no reason, which is
// exactly the bottleneck to avoid when N threads each have their own
// FileSink.

inline constexpr std::string_view basename(const char* path) noexcept {
  const std::string_view sv(path);
  const auto pos = sv.find_last_of("/\\");
  return pos == std::string_view::npos ? sv : sv.substr(pos + 1);
}

template <typename... Args>
inline void log(Level level,
                const std::source_location& loc,
                std::format_string<Args...> fmt,
                Args&&... args) noexcept {
  if (level < g_min_level.load(std::memory_order_relaxed)) {
    return;  // Filtered before any formatting work.
  }

  const auto idx                     = static_cast<std::size_t>(level);
  const auto file                    = basename(loc.file_name());
  const bool color                   = colorEnabled();
  const std::string_view dim         = color ? kColorDim : std::string_view{};
  const std::string_view reset       = color ? kColorReset : std::string_view{};
  const std::string_view level_color = color ? kLevelColor[idx] : std::string_view{};
#if DREAMOR_LOGGER_ENABLE_TIMESTAMP
  const double elapsed_ms = elapsedMs();  // captured before formatting, not after
#endif

  std::array<char, kBufferSize> buf{};  // per-call, on the stack: no shared state to race on
  char* out = buf.data();
  // Last byte reserved for the newline (written directly below), so a
  // fully-truncated message can never crowd it out.
  auto cap       = static_cast<std::ptrdiff_t>(kBufferSize) - 1;
  bool truncated = false;

  const auto advance = [&](std::format_to_n_result<char*> r) noexcept {
    if (r.size > cap) {
      truncated = true;  // this call alone needed more room than was left
    }
    const auto written = std::min(r.size, cap);
    out += written;  // NOLINT
    cap -= written;
  };
  const auto reset_buf = [&]() noexcept {
    out       = buf.data();
    cap       = static_cast<std::ptrdiff_t>(kBufferSize) - 1;
    truncated = false;
  };
  // Single spot the DREAMOR_LOGGER_ENABLE_TIMESTAMP switch touches; avoids
  // repeating the same #if in the normal path and both catch blocks below.
  const auto writeTimestamp = [&]() noexcept {
#if DREAMOR_LOGGER_ENABLE_TIMESTAMP
    advance(std::format_to_n(out, cap, "{}[+{:.3f}ms]{} ", dim, elapsed_ms, reset));
#endif
  };

  // format_to_n is not noexcept (a user std::formatter<T> can throw); caught
  // here so that can't escape this noexcept function and terminate().
  try {
    writeTimestamp();
    advance(std::format_to_n(out, cap, "{}[{}:{}]{} ", dim, file, loc.line(), reset));
    advance(std::format_to_n(out, cap, "{}[{}]{} ", level_color, kLevelTag[idx], reset));
    advance(std::format_to_n(out, cap, fmt, std::forward<Args>(args)...));
  } catch (const std::exception& e) {
    reset_buf();
    writeTimestamp();
    advance(std::format_to_n(out,
                             cap,
                             "{}[{}:{}]{} [{}] <log formatting threw: {}>",
                             dim,
                             file,
                             loc.line(),
                             reset,
                             kLevelTag[idx],
                             e.what()));
  } catch (...) {
    reset_buf();
    writeTimestamp();
    advance(std::format_to_n(out,
                             cap,
                             "{}[{}:{}]{} [{}] <log formatting threw a non-std::exception>",
                             dim,
                             file,
                             loc.line(),
                             reset,
                             kLevelTag[idx]));
  }
  // If truncated, splice a marker into the tail of what we already wrote
  // rather than appending after it - appending isn't possible since cap is
  // already 0 in the truncated case. Sacrifices a bit of the message's own
  // tail content to make the truncation visible instead of silent.
  if (truncated) {
    constexpr std::string_view kMarker = "...[TRUNCATED]";

    const auto content_len  = static_cast<std::size_t>(out - buf.data());
    const auto marker_start = content_len > kMarker.size() ? content_len - kMarker.size() : 0;
    std::copy(kMarker.begin(), kMarker.end(), buf.data() + marker_start);
    out = buf.data() + marker_start + kMarker.size();
  }
  *out++ = '\n';  // NOLINT - direct write, guaranteed in-bounds by the reserved byte above

  const auto length  = static_cast<std::size_t>(out - buf.data());
  FILE* const target = g_output;  // thread_local: no other thread can touch this
  static_cast<void>(std::fwrite(buf.data(), 1, length, target));
  // kError precedes LOG_FATAL's abort() - flush now so it survives a crash.
  if (level == Level::kError) {
    static_cast<void>(std::fflush(target));
  }
}

template <typename... Args>
[[noreturn]] inline void fatal(const std::source_location& loc,
                               std::format_string<Args...> fmt,
                               Args&&... args) noexcept {
  log(Level::kError, loc, fmt, std::forward<Args>(args)...);
  static_cast<void>(std::fflush(stderr));
  std::abort();
}

}  // namespace dreamor::logger::detail

namespace dreamor::logger {

using detail::Level;

// Anything below this level is dropped before formatting. Thread-safe to
// call from any thread at any time.
inline void setLevel(Level level) noexcept {
  detail::g_min_level.store(level, std::memory_order_relaxed);
}

inline Level getLevel() noexcept {
  return detail::g_min_level.load(std::memory_order_relaxed);
}

// Redirects future log lines FROM THE CALLING THREAD to `target`. Caller
// owns target's lifetime (open before, don't close while this thread might
// still log, close after - typically after flush()). Only affects the
// calling thread: g_output is thread_local, so there's nothing to lock here.
inline void setOutput(FILE* target) noexcept {
  detail::g_output        = target;
  detail::g_output_is_tty = DREAMOR_LOGGER_ISATTY(DREAMOR_LOGGER_FILENO(target)) != 0;
}

inline FILE* getOutput() noexcept {
  return detail::g_output;
}

// Manual flush for kInfo/kWarning lines (kError already flushes itself).
// Flushes the calling thread's current target only.
inline void flush() noexcept {
  static_cast<void>(std::fflush(detail::g_output));
}

// RAII: "log to this file for the rest of this scope."
//
//     int main() {
//       logger::FileSink sink("scheduler.log");   // opens, redirects
//       LOG_INFO("started");
//       ...
//     }   // <- flushed, previous target restored, file closed. Automatically.
//
// Construct one per file; nesting works (inner restores the outer target on
// scope exit). Thread-local like g_output: a FileSink built in one thread
// must not be passed to or destroyed from another - each parallel worker
// building its own model just constructs its own FileSink.
//
// Does not need to handle LOG_FATAL's abort(): log() already flushes the
// current target itself before calling abort(), regardless of whether this
// destructor gets to run.
class FileSink {
 public:
  explicit FileSink(const char* path, const char* mode = "a") noexcept
      : file_{std::fopen(path, mode)}, previous_{getOutput()} {
    if (file_ != nullptr) {
      setOutput(file_);
    } else {
      detail::log(Level::kError,
                  std::source_location::current(),
                  "FileSink: failed to open '{}'; logging stays on the previous target",
                  path);
    }
  }

  ~FileSink() {
    if (file_ != nullptr) {
      flush();
      setOutput(previous_);
      std::fclose(file_);
    }
  }

  FileSink(const FileSink&)            = delete;
  FileSink& operator=(const FileSink&) = delete;
  FileSink(FileSink&&)                 = delete;
  FileSink& operator=(FileSink&&)      = delete;

  [[nodiscard]] bool isOpen() const noexcept { return file_ != nullptr; }

 private:
  FILE* file_;
  FILE* previous_;
};

}  // namespace dreamor::logger

// NOLINTNEXTLINE
#define DREAMOR_LOG_INFO(fmt, ...) \
  ::dreamor::logger::detail::log(  \
      ::dreamor::logger::detail::Level::kInfo, std::source_location::current(), fmt __VA_OPT__(, ) __VA_ARGS__)

// NOLINTNEXTLINE
#define DREAMOR_LOG_WARNING(fmt, ...) \
  ::dreamor::logger::detail::log(     \
      ::dreamor::logger::detail::Level::kWarning, std::source_location::current(), fmt __VA_OPT__(, ) __VA_ARGS__)

// NOLINTNEXTLINE
#define DREAMOR_LOG_ERROR(fmt, ...) \
  ::dreamor::logger::detail::log(   \
      ::dreamor::logger::detail::Level::kError, std::source_location::current(), fmt __VA_OPT__(, ) __VA_ARGS__)

// Always fatal in every build (unlike DREAMOR_DEBUG_FATAL). Mirrors CHECK vs
// DCHECK in glog/Abseil.
// NOLINTNEXTLINE
#define DREAMOR_LOG_FATAL(fmt, ...) \
  ::dreamor::logger::detail::fatal(std::source_location::current(), fmt __VA_OPT__(, ) __VA_ARGS__)

#ifndef NDEBUG
#  define DREAMOR_DEBUG_INFO(fmt, ...) \
    ::dreamor::logger::detail::log(    \
        ::dreamor::logger::detail::Level::kInfo, std::source_location::current(), fmt __VA_OPT__(, ) __VA_ARGS__)
// Debug-only, non-fatal - mirrors glog's DLOG(ERROR).
#  define DREAMOR_DEBUG_ERROR(fmt, ...) \
    ::dreamor::logger::detail::log(     \
        ::dreamor::logger::detail::Level::kError, std::source_location::current(), fmt __VA_OPT__(, ) __VA_ARGS__)
// Debug-only AND fatal - mirrors glog's DFATAL, except this vanishes
// entirely in release rather than downgrading to a plain log.
#  define DREAMOR_DEBUG_FATAL(fmt, ...) \
    ::dreamor::logger::detail::fatal(std::source_location::current(), fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#  define DREAMOR_DEBUG_INFO(fmt, ...) static_cast<void>(0)
#  define DREAMOR_DEBUG_ERROR(fmt, ...) static_cast<void>(0)
#  define DREAMOR_DEBUG_FATAL(fmt, ...) static_cast<void>(0)
#endif
