// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Logging.hpp"

#include <filesystem>

#include <unistd.h>

namespace cw {

char levelToChar(Log::Level level) {
  switch (level) {
  case Log::Level::Debug:
    return 'D';
  case Log::Level::Error:
    return 'E';
  case Log::Level::Info:
    return 'I';
  case Log::Level::Warning:
    return 'W';
  }
  return 'U';
}

void Log::vlog(Level level, const std::source_location& location, std::string_view fmt,
               std::format_args args) {
  std::string message = std::vformat(fmt, args);
  std::filesystem::path file = location.file_name();
  std::string file_name = file.filename();
  int pid = ::getpid();
  int tid = ::gettid();
  std::fprintf(stderr, "[%s:%u] %c %d-%d %s\n", file_name.c_str(), location.line(),
               levelToChar(level), pid, tid, message.c_str());
}

} // namespace cw