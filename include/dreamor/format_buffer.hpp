// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// FormatBuffer, a buffer of each thread for one short name at a time, and NameBuilder, which builds names that share
// a root in one buffer.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#include "dreamor/internal/base/inline.hpp"
#include "dreamor/logger.hpp"

// The message of the static_assert in FormatBuffer and in NameBuilder if N is too small.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_BUFFER_SIZE_MESSAGE \
  "FormatBuffer<N> and NameBuilder<N> hold a name of up to N - 1 characters and its null terminator: N is at least 2."

namespace dreamor {

/// @section Names

/// The action that a `FormatBuffer` or a `NameBuilder` takes when a name does not fit in its buffer.
///
/// - `kThrow`: If a name does not fit, `std::runtime_error` is thrown. If a `std::formatter` throws an exception while
///   a name is written, the exception propagates.
/// - `kLogAndClear`: No exception propagates, and the functions that write a name are `noexcept`. If a name does not
///   fit, a warning is logged. `FormatBuffer::format` then returns an empty name, and `NameBuilder` cuts the name.
///   If a `std::formatter` throws an exception, a warning is logged. `FormatBuffer::format` then returns an empty
///   name, and `NameBuilder::append` ends the name after the separator.
enum class OnFormatOverflow : std::uint8_t {
  kThrow,
  kLogAndClear,
};

/// A base class that provides a buffer of `N` characters, in which a class writes one short name at a time.
///
/// A short name is, for example, the name of a constraint that a solver copies. A class derives from `FormatBuffer`
/// and calls its protected static member function `format`. `format` has the parameters of `std::format`: a format
/// string and its arguments. It writes the name into the buffer and returns a pointer to the name, which ends with a
/// null character.
///
/// The buffer belongs to the calling thread. Each thread has one buffer for each specialization
/// `FormatBuffer<N, Policy>`, and every class that derives from that specialization uses it. Therefore a `const`
/// member function can call `format`, and two threads never write into the same buffer.
///
/// The pointer that `format` returns is valid until the calling thread calls `format` of the same specialization
/// again. Pass the pointer to a function that copies the name.
///
/// A name has at most `N - 1` characters. If a name is longer, `Policy` decides what happens, as `OnFormatOverflow`
/// describes.
///
/// Takes: `N` must be at least 2.
/// Throws: `std::runtime_error` if `Policy` is `kThrow` and the name that `format` writes is longer than `N - 1`
/// characters.
template <std::size_t N, OnFormatOverflow Policy = OnFormatOverflow::kThrow>
class FormatBuffer {
  // The buffer belongs to the thread, not to the object. format() returns a pointer into the buffer. If threads shared
  // the buffer under a lock, format() would release the lock when it returns, and another thread could overwrite the
  // name before the caller copies it.
  static_assert(N >= 2, DREAMOR_INTERNAL_BUFFER_SIZE_MESSAGE);

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
  [[nodiscard]] static const char* format(const std::format_string<Args...> fmt,
                                          Args&&... args) noexcept(Policy == OnFormatOverflow::kLogAndClear) {
    if constexpr (Policy == OnFormatOverflow::kThrow) {
      return formatInto(fmt, std::forward<Args>(args)...);
    } else {
      // A user's std::formatter, such as the formatter of a primary key, may throw. This function is noexcept
      // under kLogAndClear, so it catches the exception, as NameBuilder::append does.
      try {
        return formatInto(fmt, std::forward<Args>(args)...);
      } catch (...) {
        buffer[0] = '\0';
        DREAMOR_LOG_WARNING("A formatter threw in format(); the name is empty.");
        return buffer.data();
      }
    }
  }

 private:
  template <typename... Args>
  static const char* formatInto(const std::format_string<Args...> fmt, Args&&... args) {
    const auto result = std::format_to_n(buffer.data(), kMaxLength, fmt, std::forward<Args>(args)...);
    *result.out       = '\0';
    if (static_cast<std::size_t>(result.size) > kMaxLength) {
      if constexpr (Policy == OnFormatOverflow::kThrow) {
        throw std::runtime_error("Formatting error or buffer overflow in format().");
      } else {
        buffer[0] = '\0';
        DREAMOR_LOG_WARNING("Formatting error or buffer overflow in format().");
      }
    }
    return buffer.data();
  }

  static thread_local inline std::array<char, N> buffer{};
};

/// A buffer of `N` characters in which names that start with the same root are built one after another.
///
/// Each name starts with the root. `append` adds a separator and a token to the name, once for each key. The next
/// name starts again at `rootLength()`, and its tokens overwrite the tokens of the previous name.
///
/// A builder constructed with a null root is disabled. A disabled builder writes nothing, and `name()` returns
/// `nullptr`. `restart` enables the builder.
///
/// Takes: `N` must be at least 2.
template <std::size_t N, OnFormatOverflow Policy = OnFormatOverflow::kThrow>
class NameBuilder {
  static_assert(N >= 2, DREAMOR_INTERNAL_BUFFER_SIZE_MESSAGE);

 public:
  /// The length of the longest name that the builder holds: `N - 1` characters.
  static constexpr std::size_t kMaxLength = N - 1;

  /// Constructs a builder whose names start with `root`.
  ///
  /// If `Policy` is `kLogAndClear` and `root` is longer than `kMaxLength` characters, the root is cut to `kMaxLength`
  /// characters and a warning is logged.
  ///
  /// Takes: `root` must be `nullptr` or point to a text that ends with a null character. If `root` is `nullptr`, the
  /// builder is disabled. If the text has no null character at its end, the behavior is undefined.
  /// Throws: `std::runtime_error` if `Policy` is `kThrow` and `root` is longer than `kMaxLength` characters.
  explicit NameBuilder(const char* root) noexcept(Policy == OnFormatOverflow::kLogAndClear)
      : enabled_{root != nullptr} {
    if (!enabled_) { return; }
    // The root is copied, not formatted, because formatting a C string adds nothing and could throw.
    const std::string_view text{root};
    const std::size_t copied = text.copy(buffer_.data(), kMaxLength);
    buffer_[copied]          = '\0';
    root_length_             = clamp(text.size());
  }

  /// Checks whether the builder writes names.
  ///
  /// Returns: False if the builder was constructed with a null root and `restart` has not been called since; true
  /// otherwise.
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }

  /// Makes `root` the root of the names that follow, and enables the builder.
  ///
  /// After the call, the name that the builder holds is `root` alone. If `Policy` is `kLogAndClear` and `root` is
  /// longer than `kMaxLength` characters, the root is cut to `kMaxLength` characters and a warning is logged.
  ///
  /// Returns: The length of the root, at which `append` adds the first token of a name.
  /// Throws: `std::runtime_error` if `Policy` is `kThrow` and `root` is longer than `kMaxLength` characters. The
  /// builder is then left as it was.
  std::size_t restart(std::string_view root) noexcept(Policy == OnFormatOverflow::kLogAndClear) {
    if constexpr (Policy == OnFormatOverflow::kThrow) {
      // The length is checked before anything changes, so that a root that does not fit leaves the builder as it was.
      if (root.size() > kMaxLength) { static_cast<void>(overflow()); }
    }
    enabled_                 = true;
    const std::size_t copied = root.copy(buffer_.data(), kMaxLength);
    buffer_[copied]          = '\0';
    root_length_             = clamp(root.size());
    return root_length_;
  }

  /// Returns the length of the root.
  ///
  /// The tokens of each name start at this length. The root of a builder constructed with a null root has length 0.
  [[nodiscard]] std::size_t rootLength() const noexcept { return root_length_; }

  /// Writes `separator` and then `token` into the name, starting at position `length`.
  ///
  /// The name ends after the token. What the name held from position `length` on is overwritten. `token` is written
  /// as follows:
  ///
  /// - An integer or a floating-point number, other than a `bool`, is written as `std::to_chars` writes it. A `char`
  ///   is an integer here, so it is written as its number.
  /// - A text, which is a value that converts to `std::string_view`, is copied.
  /// - Any other token is written by its `std::formatter`, as `std::format` writes it for `{}`.
  ///
  /// If the builder is disabled, `append` writes nothing and returns 0. If `Policy` is `kLogAndClear` and the name
  /// does not fit in `kMaxLength` characters, the name is cut and a warning is logged.
  ///
  /// Takes: `length` must not be greater than the length of the name that the builder holds. If it is greater, the
  /// name can end before `length` or hold characters of an earlier name. If `separator` is left out, it is `_`.
  /// Returns: The length of the name after the token. If the name was cut, `kMaxLength`.
  /// Throws: `std::runtime_error` if `Policy` is `kThrow` and the name would be longer than `kMaxLength` characters.
  template <typename Token>
  [[nodiscard]] DREAMOR_INLINE std::size_t append(
      std::size_t length,
      const Token& token,
      std::string_view separator = "_") noexcept(Policy == OnFormatOverflow::kLogAndClear) {
    // The function is always inlined: a VarTable calls it once for each column of every variable, and a call out of
    // line costs about as much as the formatting. So it holds only the fast path, an integer or a text that surely
    // fits, written by dreamor's own code with no check of the end, and leaves everything else to appendChecked(),
    // out of line. A library call here would be inlined or not as each compiler's budget allows, and the cost of a
    // name would differ by compiler: in a VarTable, GCC leaves std::to_chars out of line, and copies libstdc++'s
    // table of digits to the stack for each token.
    if (!enabled_) { return 0; }
    if constexpr (kDecimal<Token>) {
      // In a buffer too small for the widest number of the type, the fast path is left out, so that GCC does not
      // warn of writes past the end on a branch that is never taken.
      if constexpr (kWidest<Token> < kMaxLength) {
        // Each side is compared with the room that is left, so that no sum wraps around.
        constexpr std::size_t kRoom = kMaxLength - kWidest<Token>;
        if (length <= kRoom && separator.size() <= kRoom - length) [[likely]] {
          char* const end = writeDecimal(writeSeparator(buffer_.data() + length, separator), token);
          *end            = '\0';
          return static_cast<std::size_t>(end - buffer_.data());
        }
      }
    } else if constexpr (std::is_convertible_v<const Token&, std::string_view>) {
      const std::string_view text{token};
      if (length <= kMaxLength && separator.size() <= kMaxLength - length &&
          text.size() <= kMaxLength - length - separator.size()) [[likely]] {
        char* const out = writeSeparator(buffer_.data() + length, separator);
        copyText(out, text, text.size());
        out[text.size()] = '\0';
        return length + separator.size() + text.size();
      }
    }
    return appendChecked(length, token, separator);
  }

  /// Returns a pointer to the name that the builder holds.
  ///
  /// Returns: A null pointer if the builder is disabled. Otherwise, a pointer to the name, which ends with a null
  /// character. The pointer is valid until the next call of `append` or `restart` on the builder, or until the
  /// builder is destroyed.
  [[nodiscard]] const char* name() const noexcept { return enabled_ ? buffer_.data() : nullptr; }

 private:
  // An integer of up to 64 bits, which append() writes in decimal itself: a signed or unsigned integer type or char,
  // not a bool, wchar_t or charN_t. Any other number is left to std::to_chars.
  template <typename Token>
  static constexpr bool kDecimal =
      std::is_integral_v<Token> && sizeof(Token) <= sizeof(std::uint64_t) && !std::is_same_v<Token, bool> &&
      !std::is_same_v<Token, wchar_t> && !std::is_same_v<Token, char8_t> && !std::is_same_v<Token, char16_t> &&
      !std::is_same_v<Token, char32_t>;

  // The most characters that a number of an integer type takes: each of its digits and a sign.
  template <typename Token>
  static constexpr std::size_t kWidest = std::numeric_limits<Token>::digits10 + 2;

  // Two characters for each number from 00 to 99, in order. A string literal lives in the program's constant data:
  // libstdc++ keeps its table as a local array of a constexpr function, which GCC copies to the stack for each number.
  static constexpr std::string_view kDigitPairs =
      "0001020304050607080910111213141516171819202122232425262728293031323334353637383940414243444546474849"
      "5051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";

  // The slow path of append(): a token that may not fit, a floating-point number, a wider integer, or a token that
  // its std::formatter writes. It checks the end at each step. It is kept out of line so that the inlined fast path
  // stays small.
  template <typename Token>
  DREAMOR_NOINLINE std::size_t appendChecked(std::size_t length,
                                             const Token& token,
                                             std::string_view separator) noexcept(Policy ==
                                                                                  OnFormatOverflow::kLogAndClear) {
    if (length + separator.size() > kMaxLength) {
      return overflow();  // The separator does not fit.
    }
    char* out = writeSeparator(buffer_.data() + length, separator);
    length += separator.size();

    if constexpr ((std::is_integral_v<Token> && !std::is_same_v<Token, bool>) || std::is_floating_point_v<Token>) {
      // A number is written by std::to_chars, which writes what std::format writes for "{}" without parsing a format
      // string. A bool takes the general path below, because the standard deletes std::to_chars(bool), and
      // std::format writes a bool as true or false.
      const auto [ptr, ec] = std::to_chars(out, buffer_.data() + kMaxLength, token);
      if (ec != std::errc{}) {
        // If std::to_chars fails, the contents of its range are unspecified, so the name ends after the separator.
        *out = '\0';
        return overflow();
      }
      *ptr = '\0';
      return clamp(static_cast<std::size_t>(ptr - buffer_.data()));
    } else if constexpr (std::is_convertible_v<const Token&, std::string_view>) {
      // A text, such as a std::string that primaryKey() returns, is copied without parsing a format string. The
      // copy is what std::format writes for "{}", cut where std::format_to_n would cut it.
      const std::string_view text{token};
      const std::size_t copied = std::min(text.size(), kMaxLength - length);
      copyText(out, text, copied);
      out[copied] = '\0';
      return clamp(length + text.size());
    } else {
      // A user's std::formatter, such as the formatter of a primary key, may throw. This function is noexcept
      // under kLogAndClear, so it catches the exception, and the name ends after the separator.
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

  // Writes the separator at out and returns the end. A separator of one character, the common case, is stored as a
  // character: appendName() passes the separator on as a run-time string_view, so memcpy would not be folded into a
  // store and would cost a call for every token.
  DREAMOR_INLINE static char* writeSeparator(char* out, std::string_view separator) noexcept {
    if (separator.size() == 1) {
      *out = separator.front();
    } else {
      std::memcpy(out, separator.data(), separator.size());
    }
    return out + separator.size();
  }

  // Writes a minus sign at first for a negative value, moving first past it, and returns the value's magnitude.
  template <typename Int>
  DREAMOR_INLINE static std::make_unsigned_t<Int> writeSign(char*& first, Int value) noexcept {
    using Unsigned            = std::make_unsigned_t<Int>;
    const auto unsigned_value = static_cast<Unsigned>(value);
    if constexpr (std::is_signed_v<Int>) {
      if (value < 0) {
        *first++ = '-';
        return static_cast<Unsigned>(Unsigned{0} - unsigned_value);  // Also right for the most negative value.
      }
    }
    return unsigned_value;
  }

  // Writes value in decimal at first, as std::to_chars writes it, with no check of the end, and returns the end: the
  // count of digits first, by steps of four, then two digits at a time from the last.
  template <typename Int>
  DREAMOR_INLINE static char* writeDecimal(char* first, Int value) noexcept {
    // The digits are counted and written in 32 or 64 bits, so that no step narrows to a small type.
    using Wide               = std::conditional_t<(sizeof(Int) <= sizeof(std::uint32_t)), std::uint32_t, std::uint64_t>;
    constexpr Wide kTen      = 10;
    constexpr Wide kHundred  = 100;
    constexpr Wide kThousand = 1000;
    constexpr Wide kTenThousand = 10000;
    Wide rest                   = writeSign(first, value);
    std::size_t digits          = 1;
    for (Wide v = rest; v >= kTen; v /= kTenThousand, digits += 4) {
      if (v < kHundred) {
        digits += 1;
        break;
      }
      if (v < kThousand) {
        digits += 2;
        break;
      }
      if (v < kTenThousand) {
        digits += 3;
        break;
      }
    }
    std::size_t at = digits;  // The digits are written from the last.
    while (rest >= kHundred) {
      const auto pair = static_cast<std::size_t>(rest % kHundred) * 2;
      rest /= kHundred;
      first[--at] = kDigitPairs[pair + 1];
      first[--at] = kDigitPairs[pair];
    }
    if (rest >= kTen) {
      const auto pair = static_cast<std::size_t>(rest) * 2;
      first[--at]     = kDigitPairs[pair + 1];
      first[--at]     = kDigitPairs[pair];
    } else {
      first[--at] = static_cast<char>('0' + rest);
    }
    return first + digits;
  }

  // Copies the first n characters of text. A token is mostly a few characters: a short text is copied in two copies of
  // a fixed size that overlap, which compilers turn into moves, where a copy of a length unknown to the compiler is a
  // call of memcpy or, inlined by GCC for a CPU with fast string instructions, a rep movsb that costs more than the
  // copy. text and n come apart because GCC does not fold text.substr(0, n) away.
  DREAMOR_INLINE static void copyText(char* out, std::string_view text, std::size_t n) noexcept {
    constexpr std::size_t kLong = 16;
    constexpr std::size_t kWord = 8;
    const char* in              = text.data();
    if (n >= kLong) {
      std::memcpy(out, in, n);
    } else if (n >= kWord) {
      std::memcpy(out, in, kWord);
      std::memcpy(out + n - kWord, in + n - kWord, kWord);
    } else if (n >= 4) {
      std::memcpy(out, in, 4);
      std::memcpy(out + n - 4, in + n - 4, 4);
    } else if (n >= 2) {
      std::memcpy(out, in, 2);
      std::memcpy(out + n - 2, in + n - 2, 2);
    } else if (n == 1) {
      *out = *in;
    }
  }

  // Returns length if a name of length characters fits in the buffer with its null character, and reports an
  // overflow otherwise.
  [[nodiscard]] std::size_t clamp(std::size_t length) noexcept(Policy == OnFormatOverflow::kLogAndClear) {
    return length <= kMaxLength ? length : overflow();
  }

  // Reports a name longer than kMaxLength characters. Under kThrow, it throws. Under kLogAndClear, it logs a warning
  // and returns kMaxLength, and the name stays cut.
  [[nodiscard]] static std::size_t overflow() noexcept(Policy == OnFormatOverflow::kLogAndClear) {
    if constexpr (Policy == OnFormatOverflow::kThrow) {
      throw std::runtime_error("Name buffer overflow in NameBuilder::append().");
    } else {
      DREAMOR_LOG_WARNING("Name buffer overflow in NameBuilder::append(); name was truncated.");
      return kMaxLength;  // buffer_[kMaxLength] always holds a null character, so the name is terminated.
    }
  }

  std::array<char, N> buffer_{};
  std::size_t root_length_{0};
  bool enabled_{false};
};

}  // namespace dreamor
