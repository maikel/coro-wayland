// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "queries.hpp"

#include <coroutine>
#include <optional>
#include <stop_token>

namespace ms {

struct WhenStopRequestedAwaiter {
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

auto when_stop_requested() -> WhenStopRequestedAwaiter { return WhenStopRequestedAwaiter{}; }

template <class Fn> auto upon_stop_requested(Fn fn) -> Task<void> {
  co_await when_stop_requested();
  fn();
}

} // namespace ms