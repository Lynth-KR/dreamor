// SPDX-License-Identifier: Apache-2.0
// File: format_buffer.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// Inherit privately:
//
//     class FooFooFoo : private dreamor::FormatBuffer<256, dreamor::OnFormatOverflow::kLogAndClear> {
//       void f() { ... format("x_length_{}", eqid) ... }
//     };
#pragma once

#include <array>
#include <charconv>  // IWYU pragma: keep // For std::to_chars fast path
#include <cstddef>
#include <cstdint>      // IWYU pragma: keep // For std::uint8_t
#include <cstring>      // IWYU pragma: keep // For std::memcpy (separator)
#include <format>       // IWYU pragma: keep // For std::format_to_n
#include <stdexcept>    // IWYU pragma: keep // For std::runtime_error
#include <string_view>  // IWYU pragma: keep // For the separator parameter
#include <type_traits>  // IWYU pragma: keep // For std::is_integral_v / is_floating_point_v
#include <utility>      // IWYU pragma: keep // For std::forward

#include "dreamor/logger/logger_light.hpp"  // IWYU pragma: keep // For DREAMOR_LOG_WARNING

namespace dreamor::general {

enum class OnFormatOverflow : std::uint8_t {
  kThrow,        // Throw std::runtime_error. Do NOT use from a noexcept caller.
  kLogAndClear,  // Log the error and return an empty string. format() is noexcept.
};

// Fixed-size scratch buffer for building short names (CPLEX variable / constraint names).
//
// IMPORTANT: the returned pointer aliases the internal buffer and is only valid until
// the next format() call on the SAME object. Consume it immediately; never store it.
template <std::size_t N, OnFormatOverflow Policy = OnFormatOverflow::kThrow>
class FormatBuffer {
  static_assert(N >= 2, "Buffer must hold at least one character plus a null terminator.");

 protected:
  static constexpr std::size_t kNameBufferSize = N;
  static constexpr std::size_t kMaxLength      = N - 1;

  FormatBuffer()                                   = default;
  ~FormatBuffer()                                  = default;
  FormatBuffer(const FormatBuffer&)                = default;
  FormatBuffer& operator=(const FormatBuffer&)     = default;
  FormatBuffer(FormatBuffer&&) noexcept            = default;
  FormatBuffer& operator=(FormatBuffer&&) noexcept = default;

  template <typename... Args>
  [[nodiscard]] const char* format(const std::format_string<Args...> fmt,
                                   Args&&... args) noexcept(Policy == OnFormatOverflow::kLogAndClear) {
    const auto result = std::format_to_n(buffer_.data(), kMaxLength, fmt, std::forward<Args>(args)...);
    *result.out       = '\0';  // Null-terminate the string
    if (static_cast<std::size_t>(result.size) >= kMaxLength) {
      if constexpr (Policy == OnFormatOverflow::kThrow) {
        throw std::runtime_error("Formatting error or buffer overflow in format().");
      } else {
        buffer_[0] = '\0';  // Ensure it's null-terminated
        DREAMOR_LOG_WARNING("Formatting error or buffer overflow in format().");
      }
    }
    return buffer_.data();
  }

 private:
  std::array<char, N> buffer_{};
};

// Builds a dotted/underscored name incrementally while walking a tree, using ONE buffer
// for the whole traversal instead of one per node: each level appends "_<token>" at the
// offset its parent reports, so siblings simply overwrite each other's suffix.
//
// A null root name disables the builder: name() then yields nullptr, which Concert
// accepts as "unnamed", and no formatting is attempted.
template <std::size_t N, OnFormatOverflow Policy = OnFormatOverflow::kThrow>
class NameBuilder {
  static_assert(N >= 2, "Buffer must hold at least one character plus a null terminator.");

 public:
  static constexpr std::size_t kMaxLength = N - 1;

  explicit NameBuilder(const char* root) noexcept(Policy == OnFormatOverflow::kLogAndClear)
      : enabled_{root != nullptr} {
    if (!enabled_) { return; }
    // A plain copy: formatting a C string would add nothing but a way to throw.
    const std::string_view text{root};
    const std::size_t copied = text.copy(buffer_.data(), kMaxLength);
    buffer_[copied]          = '\0';
    root_length_             = clamp(text.size());
  }

  [[nodiscard]] bool enabled() const noexcept { return enabled_; }

  [[nodiscard]] std::size_t rootLength() const noexcept { return root_length_; }

  // Appends "<separator><token>" at `length` and returns the new length.
  // Separator defaults to "_" (the shape every existing call site relies
  // on) but can be any compile-time-sized string, single or multi-char
  // ("-", "__", ...).
  //
  // Fast path for arithmetic Token (the common case - entity ids, indices):
  // std::to_chars instead of the general formatter, ~5x faster (measured).
  // Output is byte-identical to std::format's default "{}" for these types
  // (both are the locale-independent shortest round-trip representation),
  // so this changes performance only, not the names produced. Text is
  // copied as it is; anything else (ADL keyName() results with a custom
  // formatter) still goes through std::format_to_n.
  // Always inlined: a VarTable calls it once a column for every variable, and out of line the call
  // cost about as much as the formatting.
  template <typename Token>
  [[nodiscard, gnu::always_inline]] std::size_t append(
      std::size_t length,
      const Token& token,
      std::string_view separator = "_") noexcept(Policy == OnFormatOverflow::kLogAndClear) {
    if (!enabled_) { return 0; }
    if (length + separator.size() > kMaxLength) {
      return clamp(kMaxLength);  // No room even for the separator.
    }
    char* out = buffer_.data() + length;
    std::memcpy(out, separator.data(), separator.size());
    out += separator.size();
    length += separator.size();

    if constexpr ((std::is_integral_v<Token> && !std::is_same_v<Token, bool>) || std::is_floating_point_v<Token>) {
      // bool excluded: std::to_chars has no bool overload (it's explicitly
      // deleted in libstdc++), even though std::is_integral_v<bool> is
      // true. Falls through to the general path below, matching what
      // std::format's "{}" already did for bool before this fast path
      // existed ("true"/"false").
      const auto [ptr, ec] = std::to_chars(out, buffer_.data() + kMaxLength, token);
      if (ec != std::errc{}) {
        // to_chars' contents on failure are UNSPECIFIED by the standard -
        // this toolchain leaves the range untouched, but that's not
        // guaranteed everywhere. Don't trust it either way: truncate
        // cleanly right after the separator, which is the last position we
        // definitely wrote ourselves.
        *out = '\0';
        return clamp(kMaxLength);
      }
      *ptr = '\0';
      return clamp(static_cast<std::size_t>(ptr - buffer_.data()));
    } else if constexpr (std::is_convertible_v<const Token&, std::string_view>) {
      // Text (a keyName() returning a std::string, say) is copied: what std::format's "{}" writes
      // for it, cut where format_to_n would cut it, without parsing a format string per token.
      const std::string_view text{token};
      const std::size_t copied = text.copy(out, kMaxLength - length);
      out[copied]              = '\0';
      return clamp(length + text.size());
    } else {
      // A user's std::formatter (a keyName() result) may throw. Under kLogAndClear this is
      // noexcept, so the throw ends here - the name stops after the separator, as above.
      try {
        const auto result = std::format_to_n(out, static_cast<std::ptrdiff_t>(kMaxLength - length), "{}", token);
        *result.out       = '\0';
        return clamp(length + static_cast<std::size_t>(result.size));
      } catch (...) {
        if constexpr (Policy == OnFormatOverflow::kThrow) {
          throw;
        } else {
          DREAMOR_LOG_WARNING("A name token's formatter threw in NameBuilder::append(); name was truncated.");
          *out = '\0';
          return length;
        }
      }
    }
  }

  // Valid until the next append(); hand it straight to IloNumVar, which copies it.
  [[nodiscard]] const char* name() const noexcept { return enabled_ ? buffer_.data() : nullptr; }

 private:
  [[nodiscard]] std::size_t clamp(std::size_t length) noexcept(Policy == OnFormatOverflow::kLogAndClear) {
    if (length < kMaxLength) { return length; }
    if constexpr (Policy == OnFormatOverflow::kThrow) {
      throw std::runtime_error("Name buffer overflow in NameBuilder::append().");
    } else {
      DREAMOR_LOG_WARNING("Name buffer overflow in NameBuilder::append(); name was truncated.");
      return kMaxLength;  // Already null-terminated; keep going with the truncated name.
    }
  }

  std::array<char, N> buffer_{};
  std::size_t root_length_{0};
  bool enabled_{false};
};

}  // namespace dreamor::general
