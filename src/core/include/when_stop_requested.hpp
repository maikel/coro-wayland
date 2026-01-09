// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "queries.hpp"

#include <coroutine>
#include <optional>
#include <stop_token>

namespace ms {
template <class AwaitingPromise> struct WhenStopRequestedAwaiter {
  WhenStopRequestedAwaiter() = default;

  static auto await_ready() noexcept -> std::false_type { return {}; }

  auto await_suspend(std::coroutine_handle<AwaitingPromise> handle) noexcept
      -> std::coroutine_handle<> {
    std::stop_token stopToken = ms::get_stop_token(ms::get_env(handle.promise()));
    if (stopToken.stop_requested()) {
      return handle;
    }
    mStopCallback.emplace(stopToken, OnStopRequested{handle});
    return std::noop_coroutine();
  }

  void await_resume() noexcept {}

  struct OnStopRequested {
    explicit OnStopRequested(std::coroutine_handle<AwaitingPromise> handle) noexcept
        : mHandle(handle) {}

    void operator()() noexcept { mHandle.resume(); }

    std::coroutine_handle<AwaitingPromise> mHandle;
  };

  std::optional<std::stop_callback<OnStopRequested>> mStopCallback;
};

struct WhenStopRequestedSender {
  template <class AwaitingPromise>
  auto connect(AwaitingPromise& /* promise */) const noexcept
      -> WhenStopRequestedAwaiter<AwaitingPromise> {
    return WhenStopRequestedAwaiter<AwaitingPromise>{};
  }
};

inline auto when_stop_requested() -> WhenStopRequestedSender { return WhenStopRequestedSender{}; }

template <class Fn> auto upon_stop_requested(Fn fn) -> Task<void> {
  co_await when_stop_requested();
  fn();
}

} // namespace ms