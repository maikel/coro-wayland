// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <format>
#include <source_location>
#include <string_view>

namespace cw {

struct Log {
  enum class Level { Debug, Info, Warning, Error };

  static void vlog(Level, const std::source_location& location, std::string_view fmt,
                   std::format_args args);

  template <typename... Args>
  static void log(Level level, const std::source_location& location, std::string_view fmt,
                  Args&&... args) {
    vlog(level, location, fmt, std::make_format_args(args...));
  }

  template <typename... Args> struct d {
    d(std::string_view fmt, Args&&... args,
      const std::source_location& location = std::source_location::current()) {
      log(Level::Debug, location, fmt, std::forward<Args>(args)...);
    }
  };
  template <typename... Args> d(std::string_view, Args&&...) -> d<Args...>;

  template <typename... Args> struct i {
    i(std::string_view fmt, Args&&... args,
      const std::source_location& location = std::source_location::current()) {
      log(Level::Info, location, fmt, std::forward<Args>(args)...);
    }
  };
  template <typename... Args> i(std::string_view, Args&&...) -> i<Args...>;

  template <typename... Args> struct w {
    w(std::string_view fmt, Args&&... args,
      const std::source_location& location = std::source_location::current()) {
      log(Level::Warning, location, fmt, std::forward<Args>(args)...);
    }
  };
  template <typename... Args> w(std::string_view, Args&&...) -> w<Args...>;

  template <typename... Args> struct e {
    e(std::string_view fmt, Args&&... args,
      const std::source_location& location = std::source_location::current()) {
      log(Level::Error, location, fmt, std::forward<Args>(args)...);
    }
  };
  template <typename... Args> e(std::string_view, Args&&...) -> e<Args...>;
};

} // namespace cw