// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <coroutine>
#include <system_error>

namespace ms {

class JustStoppedAwaiter {
public:
  explicit JustStoppedAwaiter() {}

  static auto await_ready() noexcept -> std::false_type { return {}; }

  template <class AwaitingPromise>
  auto await_suspend(std::coroutine_handle<AwaitingPromise> handle) noexcept -> void {
    if constexpr (requires { handle.promise().unhandled_stopped(); }) {
      handle.promise().unhandled_stopped();
    } else {
      throw std::system_error{std::make_error_code(std::errc::operation_canceled)};
    }
  }

  void await_resume() noexcept {}
};

inline auto just_stopped() noexcept -> JustStoppedAwaiter { return JustStoppedAwaiter{}; }

} // namespace ms