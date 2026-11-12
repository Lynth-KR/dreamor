// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The lines that dreamor's logger writes on this thread while a LogCapture lives, for a test to read.
#pragma once

#include <array>
#include <cstddef>
#include <cstdio>
#include <string>

#include "dreamor/logger.hpp"

// Sends this thread's log lines to a temporary file while it lives. Its destructor sends them back to the output
// they went to before and closes the file.
class LogCapture {
 public:
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): a C file, which the destructor closes
  LogCapture() : before_{dreamor::logger::getOutput()}, file_{openTemporary()} {
    if (file_ != nullptr) { dreamor::logger::setOutput(file_); }
  }

  LogCapture(const LogCapture&)            = delete;
  LogCapture& operator=(const LogCapture&) = delete;
  LogCapture(LogCapture&&)                 = delete;
  LogCapture& operator=(LogCapture&&)      = delete;

  ~LogCapture() {
    dreamor::logger::setOutput(before_);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the file that the constructor opened
    if (file_ != nullptr) { static_cast<void>(std::fclose(file_)); }
  }

  // Checks whether the temporary file was opened. If it was not, the lines go to the output they went to before.
  [[nodiscard]] bool isOpen() const { return file_ != nullptr; }

  // Returns every line written since the construction, after flushing the logger.
  [[nodiscard]] std::string text() const {
    std::string text;
    if (file_ == nullptr) { return text; }
    dreamor::logger::flush();
    static_cast<void>(std::fseek(file_, 0, SEEK_SET));
    std::array<char, 256> chunk{};
    for (std::size_t read = 0; (read = std::fread(chunk.data(), 1, chunk.size(), file_)) > 0;) {
      text.append(chunk.data(), read);
    }
    static_cast<void>(std::fseek(file_, 0, SEEK_END));  // a write after a read needs a seek between them
    return text;
  }

 private:
  // A temporary file, or null. The MSVC CRT deprecates tmpfile, so Windows takes tmpfile_s (as differential.hpp's
  // environmentText takes _dupenv_s).
  static std::FILE* openTemporary() {
#ifdef _WIN32
    std::FILE* file = nullptr;
    return tmpfile_s(&file) == 0 ? file : nullptr;
#else
    return std::tmpfile();  // NOLINT(cppcoreguidelines-owning-memory): the destructor closes it
#endif
  }

  std::FILE* before_;
  std::FILE* file_;
};
