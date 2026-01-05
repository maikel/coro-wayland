// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <format>
#include <source_location>
#include <string_view>

namespace ms {

struct Log {
  enum class Level { Debug, Info, Warning, Error };

  static void vlog(Level, const std::source_location& location, std::string_view fmt,
                   std::format_args args);

  template <typename... Args>
  static void log(Level level, const std::source_location& location, std::string_view fmt,
                  Args&&... args) {
    vlog(level, location, fmt, std::make_format_args(std::forward<Args>(args)...));
  }

  template <typename... Args> struct d {
    d(std::string_view fmt, Args&&... args,
      const std::source_location& location = std::source_location::current()) {
      log(Level::Debug, location, fmt, std::forward<Args>(args)...);
    }
  };

  template <typename... Args> d(std::string_view, Args&&...) -> d<Args...>;
};

} // namespace ms