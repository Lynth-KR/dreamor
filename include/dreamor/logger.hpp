// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The logger, which writes one line for each call to the target of the calling thread and drops lines below a
// level, and the DREAMOR_LOG_* macros.
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

#ifdef _WIN32
#  include <io.h>
#  include <share.h>
#else
#  include <unistd.h>
#endif

/// @section Logger
///
/// The parts of this section write messages to a log, one line for each call of a `DREAMOR_LOG_` or `DREAMOR_DEBUG_`
/// macro. A line has the form `[+123.456ms] [file:line] [LEVEL] message`:
///
/// - `+123.456ms` is the time since the process wrote its first line, in milliseconds.
///   `DREAMOR_LOGGER_ENABLE_TIMESTAMP` can leave it out.
/// - `file:line` is the name of the file that holds the call, without its directory, and the line of the call.
/// - `LEVEL` is `INFO`, `WARN` or `ERROR`.
/// - `message` is the format string and its arguments, formatted as by `std::format`.
///
/// Each thread writes its lines to its own target. A thread's target is `stderr` until `setOutput` or a `FileSink`
/// changes it. Therefore threads that build models at the same time can each write to a file of their own.
///
/// A line whose level is below the level that `setLevel` sets is dropped before its message is formatted. An error
/// line is flushed as soon as it is written.
///
/// A line is colored if its target is a terminal and the environment variable `NO_COLOR` is not set or is empty.
/// The logger reads `NO_COLOR` once, when the process writes its first line. With Emscripten, no line is colored.
///
/// A line has at most 1023 bytes before its newline, color codes included. A longer line is cut, and its last bytes
/// are replaced by `...[TRUNCATED]`.
///
/// If formatting the message throws an exception, the exception does not propagate. The line then holds a note in
/// place of the message. If the exception is a `std::exception`, the note includes its `what()`.
///
/// Guide: guide/names-and-logging.md

#ifndef DREAMOR_LOGGER_ENABLE_TIMESTAMP
/// Selects whether a line starts with the time since the process wrote its first line, such as `[+123.456ms]`.
///
/// If the macro is 1, which is the default, each line starts with the time. If the macro is 0, no line has the time,
/// and the logger does not read the clock. To set it to 0, define the macro before the header is included, or on
/// the compiler's command line. Every translation unit of a program must see the same value.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a switch that users set with -D
#  define DREAMOR_LOGGER_ENABLE_TIMESTAMP 1
#endif

namespace dreamor::logger::detail {

enum class Level : std::uint8_t { kInfo = 0, kWarning = 1, kError = 2 };

inline constexpr std::uint16_t kBufferSize                   = 1024;
inline constexpr std::string_view kColorReset                = "\033[0m";
inline constexpr std::string_view kColorDim                  = "\033[90m";
inline constexpr std::array<std::string_view, 3> kLevelColor = {"\033[32m", "\033[33m", "\033[31m"};
inline constexpr std::array<std::string_view, 3> kLevelTag   = {"INFO", "WARN", "ERROR"};

// Checks whether `stream` is a terminal, to decide whether to color the output. Each platform has its own function
// for this. Emscripten's isatty() returns true for its console device wherever the output goes, also to a browser's
// console, which prints ANSI codes as text, and to a file. Therefore the answer with Emscripten is always false.
inline bool isTerminal(FILE* stream) noexcept {
#ifdef __EMSCRIPTEN__
  static_cast<void>(stream);
  return false;
#elif defined(_WIN32)
  return _isatty(_fileno(stream)) != 0;
#else
  return isatty(fileno(stream)) != 0;
#endif
}

// Checks whether the environment variable `name` is set and not empty. The MSVC CRT deprecates getenv, so Windows
// uses _dupenv_s.
inline bool isEnvSet(const char* name) noexcept {
#ifdef _WIN32
  char* value      = nullptr;
  std::size_t size = 0;
  const bool set   = _dupenv_s(&value, &size, name) == 0 && value != nullptr && value[0] != '\0';
  std::free(value);
  return set;
#else
  const char* value = std::getenv(name);
  return value != nullptr && value[0] != '\0';
#endif
}

// Opens a file as fopen does. The MSVC CRT deprecates fopen, and its fopen_s opens the file unshared, so that no
// other program could read the log while it is open. Therefore Windows uses _fsopen with sharing on.
inline FILE* openFile(const char* path, const char* mode) noexcept {
#ifdef _WIN32
  return _fsopen(path, mode, _SH_DENYNO);
#else
  return std::fopen(path, mode);  // NOLINT(cppcoreguidelines-owning-memory): the caller owns the FILE
#endif
}

// The state of each thread below is constinit, initialized by a constant. Under the MSVC ABI (cl.exe and clang-cl),
// the dynamic initializer of a thread_local runs from a TLS callback at the start of every thread. These threads
// include the loader's worker threads, which start before the CRT has set up stdio. There,
// _isatty(_fileno(stderr)) is an invalid parameter, which ends the process before main. A constant needs no callback.

// The target of each thread. nullptr stands for stderr, because stderr is not a constant. The target is
// thread_local so that threads that build models in parallel can each write to their own file. A new thread starts
// with stderr.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): per-thread state, by design
inline constinit thread_local FILE* g_output = nullptr;

inline FILE* output() noexcept {
  return g_output != nullptr ? g_output : stderr;
}

// Whether the target is a terminal. It is checked on the first line after a thread starts or changes its target, and
// then kept, because isatty() is a system call and would be a real cost on every line.
enum class Terminal : std::uint8_t { kUnknown, kNo, kYes };
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): per-thread state, by design
inline constinit thread_local Terminal g_output_terminal = Terminal::kUnknown;

inline bool outputIsTerminal() noexcept {
  if (g_output_terminal == Terminal::kUnknown) {
    g_output_terminal = isTerminal(output()) ? Terminal::kYes : Terminal::kNo;
  }
  return g_output_terminal == Terminal::kYes;
}

// NO_COLOR (https://no-color.org) is read once, by a static local that is initialized on first use without a race.
// It is the same for the whole process, because it is a setting of the environment, not of one model.
inline bool noColorRequested() noexcept {
  static const bool requested = isEnvSet("NO_COLOR");
  return requested;
}

inline bool colorEnabled() noexcept {
  return !noColorRequested() && outputIsTerminal();
}

// The clock is steady_clock, because the logger shows durations, not the date and time. A change of the system
// clock, such as a step by NTP, must not change a duration.
inline std::chrono::steady_clock::time_point startTime() noexcept {
  static const auto t0 = std::chrono::steady_clock::now();
  return t0;
}

// Returns the milliseconds since the process logged its first line. Two lines around a block of code then show how
// long the block took, with no std::chrono code at the call site.
inline double elapsedMs() noexcept {
  const auto t0  = startTime();  // Read before now, so that t0 <= now and the first line never shows -0.000ms.
  const auto now = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(now - t0).count();
}

// Relaxed order is enough: the level decides only whether a line is written, and guards no other data.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): process-wide and atomic, by design
inline std::atomic<Level> g_min_level{Level::kInfo};

// The logger has no mutex. POSIX and the MSVC CRT already lock each stdio call on one stream, and a mutex of the
// logger would make threads that write to different files wait for each other.

constexpr std::string_view basename(const char* path) noexcept {
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
    return;  // The line is dropped before anything is formatted.
  }

  const auto idx                     = static_cast<std::size_t>(level);
  const auto file                    = basename(loc.file_name());
  const bool color                   = colorEnabled();
  const std::string_view dim         = color ? kColorDim : std::string_view{};
  const std::string_view reset       = color ? kColorReset : std::string_view{};
  const std::string_view level_color = color ? kLevelColor[idx] : std::string_view{};
#if DREAMOR_LOGGER_ENABLE_TIMESTAMP
  const double elapsed_ms = elapsedMs();  // Read before formatting, so that the time does not include it.
#endif

  std::array<char, kBufferSize> buf{};  // On the stack for each call, so that no thread shares it.
  char* out = buf.data();
  // The last byte is kept for the newline, so that a message that fills the buffer cannot take its place.
  auto cap       = static_cast<std::ptrdiff_t>(kBufferSize) - 1;
  bool truncated = false;

  const auto advance = [&](std::format_to_n_result<char*> r) noexcept {
    if (r.size > cap) {
      truncated = true;  // This call needed more room than was left.
    }
    const auto written = std::min(r.size, cap);
    out += written;
    cap -= written;
  };
  const auto reset_buf = [&]() noexcept {
    out       = buf.data();
    cap       = static_cast<std::ptrdiff_t>(kBufferSize) - 1;
    truncated = false;
  };
  // The only place that DREAMOR_LOGGER_ENABLE_TIMESTAMP changes, so that the normal path and both catch blocks
  // below do not repeat the #if.
  const auto write_timestamp = [&] {
#if DREAMOR_LOGGER_ENABLE_TIMESTAMP
    advance(std::format_to_n(out, cap, "{}[+{:.3f}ms]{} ", dim, elapsed_ms, reset));
#endif
  };

  // format_to_n is not noexcept, because a user's std::formatter can throw. The exception is caught here, because
  // an exception that leaves this noexcept function calls std::terminate().
  try {
    write_timestamp();
    advance(std::format_to_n(out, cap, "{}[{}:{}]{} ", dim, file, loc.line(), reset));
    advance(std::format_to_n(out, cap, "{}[{}]{} ", level_color, kLevelTag[idx], reset));
    advance(std::format_to_n(out, cap, fmt, std::forward<Args>(args)...));
  } catch (const std::exception& e) {
    // The fallback line formats only standard types, but no exception may leave this function. If the fallback
    // throws too, the line holds only the newline.
    try {
      reset_buf();
      write_timestamp();
      advance(std::format_to_n(out,
                               cap,
                               "{}[{}:{}]{} [{}] <log formatting threw: {}>",
                               dim,
                               file,
                               loc.line(),
                               reset,
                               kLevelTag[idx],
                               e.what()));
    } catch (...) { reset_buf(); }
  } catch (...) {
    try {
      reset_buf();
      write_timestamp();
      advance(std::format_to_n(out,
                               cap,
                               "{}[{}:{}]{} [{}] <log formatting threw a non-std::exception>",
                               dim,
                               file,
                               loc.line(),
                               reset,
                               kLevelTag[idx]));
    } catch (...) { reset_buf(); }
  }
  // If the line was cut, the marker overwrites the end of the line. It cannot be appended, because cap is already 0
  // when the line was cut. The last characters of the message are lost, but the cut is visible.
  if (truncated) {
    constexpr std::string_view kMarker = "...[TRUNCATED]";

    const auto content_len  = static_cast<std::size_t>(out - buf.data());
    const auto marker_start = content_len > kMarker.size() ? content_len - kMarker.size() : 0;
    std::ranges::copy(kMarker, buf.data() + marker_start);
    out = buf.data() + marker_start + kMarker.size();
  }
  *out++ = '\n';  // In bounds: this is the byte kept above.

  const auto length  = static_cast<std::size_t>(out - buf.data());
  FILE* const target = output();  // The target is thread_local, so no other thread can change it.
  static_cast<void>(std::fwrite(buf.data(), 1, length, target));
  // DREAMOR_LOG_FATAL calls abort() after an error line, so the line is flushed now, before the abort can lose it.
  if (level == Level::kError) { static_cast<void>(std::fflush(target)); }
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

/// The level of a line.
///
/// The levels, from the lowest to the highest, are `kInfo`, `kWarning` and `kError`.
using detail::Level;

/// Sets the level below which lines are dropped.
///
/// The level applies to the lines of every thread. A line below `level` is dropped before its message is formatted.
/// The arguments of the macro that writes the line are still evaluated. The level is `kInfo` until `setLevel` is
/// called, so no line is dropped.
inline void setLevel(Level level) noexcept {
  detail::g_min_level.store(level, std::memory_order_relaxed);
}

/// Returns the level below which lines are dropped.
inline Level getLevel() noexcept {
  return detail::g_min_level.load(std::memory_order_relaxed);
}

/// Makes `target` the target of the lines that the calling thread writes after this call.
///
/// The logger neither flushes nor closes the previous target, and it never closes `target`.
///
/// Takes: `target` must be `nullptr` or a file that is open for writing. If `target` is `nullptr`, the lines go to
/// `stderr`. If the calling thread writes a line to `target` after the file is closed, the behavior is undefined.
inline void setOutput(FILE* target) noexcept {
  detail::g_output          = target;
  detail::g_output_terminal = detail::Terminal::kUnknown;
}

/// Returns the target of the calling thread's lines.
///
/// Returns: `stderr` if the calling thread has no target of its own.
inline FILE* getOutput() noexcept {
  return detail::output();
}

/// Flushes the target of the calling thread's lines.
///
/// An error line is flushed when it is written, without this call.
inline void flush() noexcept {
  static_cast<void>(std::fflush(detail::output()));
}

/// An object that sends the calling thread's lines to a file while the object exists.
///
/// The constructor opens the file and makes it the thread's target. The destructor flushes and closes the file, and
/// makes the previous target the thread's target again. If the file could not be opened, the destructor does
/// nothing.
///
/// `FileSink`s can be nested: a `FileSink` constructed while another one exists sends the lines to its own file
/// until it is destroyed. A `FileSink` cannot be copied or moved.
///
/// If the `FileSink`s of one thread are not destroyed in the reverse order of their construction, the behavior is
/// undefined. If a `FileSink` is destroyed by a thread other than the one that constructed it, the behavior is
/// undefined.
class FileSink {
  // FileSink does not need to handle abort(): fatal() writes an error line, which flushes the target, before it aborts.
 public:
  /// Constructs a `FileSink` that opens the file `path` with `mode` and sends the calling thread's lines to it.
  ///
  /// If the file cannot be opened, an error line says so, and the thread's lines go to the target that they went to
  /// before.
  ///
  /// Takes: `mode` must be a mode that `std::fopen` accepts. If `mode` is left out, it is `"a"`, which appends to the
  /// file.
  explicit FileSink(const char* path, const char* mode = "a") noexcept
      : file_{detail::openFile(path, mode)}, previous_{getOutput()} {
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
      std::fclose(file_);  // NOLINT(cppcoreguidelines-owning-memory): FileSink owns the FILE it opened
    }
  }

  FileSink(const FileSink&)            = delete;
  FileSink& operator=(const FileSink&) = delete;
  FileSink(FileSink&&)                 = delete;
  FileSink& operator=(FileSink&&)      = delete;

  /// Checks whether the constructor opened the file.
  ///
  /// Returns: True if the constructor opened the file; false otherwise.
  [[nodiscard]] bool isOpen() const noexcept { return file_ != nullptr; }

 private:
  FILE* file_;
  FILE* previous_;
};

}  // namespace dreamor::logger

// Only a macro can capture the call site of a log call and check its format string at compile time, and only a
// macro can remove the DEBUG calls and their arguments under NDEBUG. The format string is the first argument in
// __VA_ARGS__, not a parameter of its own: a parameter of its own would need __VA_OPT__, which MSVC's preprocessor
// accepts only under /Zc:preprocessor.
// NOLINTBEGIN(cppcoreguidelines-macro-usage): a log call must be a macro, as said above
/// Writes an info line.
///
/// The first argument is a format string, and the other arguments are its arguments, as for `std::format`. A format
/// string that does not match its arguments does not compile. The line records the file and the line of the call.
#define DREAMOR_LOG_INFO(...) \
  ::dreamor::logger::detail::log(::dreamor::logger::detail::Level::kInfo, std::source_location::current(), __VA_ARGS__)

/// Writes a warning line.
///
/// The arguments are the same as for `DREAMOR_LOG_INFO`.
#define DREAMOR_LOG_WARNING(...)  \
  ::dreamor::logger::detail::log( \
      ::dreamor::logger::detail::Level::kWarning, std::source_location::current(), __VA_ARGS__)

/// Writes an error line and flushes the target.
///
/// The arguments are the same as for `DREAMOR_LOG_INFO`.
#define DREAMOR_LOG_ERROR(...) \
  ::dreamor::logger::detail::log(::dreamor::logger::detail::Level::kError, std::source_location::current(), __VA_ARGS__)

/// Writes an error line, flushes the target and `stderr`, and then aborts the program.
///
/// The arguments are the same as for `DREAMOR_LOG_INFO`. The program aborts in every build, whether `NDEBUG` is
/// defined or not.
#define DREAMOR_LOG_FATAL(...) ::dreamor::logger::detail::fatal(std::source_location::current(), __VA_ARGS__)

#ifndef NDEBUG
/// Writes an info line in a debug build, and does nothing in other builds.
///
/// A debug build is one in which `NDEBUG` is not defined when this header is first included in a translation unit.
/// In a debug build, the macro does what `DREAMOR_LOG_INFO` does. If `NDEBUG` is defined, the macro's arguments
/// are not evaluated.
#  define DREAMOR_DEBUG_INFO(...)   \
    ::dreamor::logger::detail::log( \
        ::dreamor::logger::detail::Level::kInfo, std::source_location::current(), __VA_ARGS__)
/// Writes an error line in a debug build, and does nothing in other builds.
///
/// In a debug build, the macro does what `DREAMOR_LOG_ERROR` does. If `NDEBUG` is defined, the macro's arguments are
/// not evaluated.
#  define DREAMOR_DEBUG_ERROR(...)  \
    ::dreamor::logger::detail::log( \
        ::dreamor::logger::detail::Level::kError, std::source_location::current(), __VA_ARGS__)
/// Writes an error line and aborts the program in a debug build, and does nothing in other builds.
///
/// In a debug build, the macro does what `DREAMOR_LOG_FATAL` does. If `NDEBUG` is defined, the macro's arguments
/// are not evaluated. dreamor's debug-build checks stop a program in the same way.
#  define DREAMOR_DEBUG_FATAL(...) ::dreamor::logger::detail::fatal(std::source_location::current(), __VA_ARGS__)
#else
#  define DREAMOR_DEBUG_INFO(...) static_cast<void>(0)
#  define DREAMOR_DEBUG_ERROR(...) static_cast<void>(0)
#  define DREAMOR_DEBUG_FATAL(...) static_cast<void>(0)
#endif
// NOLINTEND(cppcoreguidelines-macro-usage)
