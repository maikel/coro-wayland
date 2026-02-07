// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "ImmovableBase.hpp"
#include "queries.hpp"

#include <array>
#include <coroutine>
#include <optional>
#include <stop_token>
#include <tuple>

namespace cw {
template <class Fn> class EmplaceFrom {
public:
  using result_type = std::invoke_result_t<Fn>;

  EmplaceFrom(Fn fn) noexcept : mFn(std::move(fn)) {}

  operator result_type() { return mFn(); }

private:
  Fn mFn;
};

template <std::size_t N> struct WhenStopRequestedAwaiter : ImmovableBase {
  struct State;
  struct OnStopRequested {
    explicit OnStopRequested(std::shared_ptr<State> state) noexcept : mState(std::move(state)) {}

    void operator()() noexcept {
      if (!mState->mStopRequested.test_and_set()) {
        std::shared_ptr<State> state = std::move(mState);
        state->mStopCallbacks.reset();
        state->mHandle.resume();
      }
    }

    std::shared_ptr<State> mState;
  };

  struct State {
    std::coroutine_handle<> mHandle{nullptr};
    std::atomic_flag mStopRequested{false};
    std::stop_source mStopSource{};
    std::optional<std::array<std::stop_callback<OnStopRequested>, N + 1>> mStopCallbacks{};
  };

  explicit WhenStopRequestedAwaiter(std::array<std::stop_token, N> stopTokens) noexcept
      : mStopTokens{stopTokens} {}

  static auto await_ready() noexcept -> std::false_type { return {}; }

  template <class Promise>
  auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> std::coroutine_handle<> {
    auto state = std::make_shared<State>();
    state->mHandle = handle;
    std::stop_token stopToken = cw::get_stop_token(cw::get_env(handle.promise()));
    if (stopToken.stop_requested()) {
      return handle;
    }
    state->mStopCallbacks.emplace(EmplaceFrom{[&] {
      return std::apply(
          [state, stopToken](auto... token) {
            return std::array<std::stop_callback<OnStopRequested>, N + 1>{
                std::stop_callback<OnStopRequested>{stopToken, OnStopRequested{state}},
                std::stop_callback<OnStopRequested>{token, OnStopRequested{state}}...};
          },
          mStopTokens);
    }});
    return std::noop_coroutine();
  }

  void await_resume() noexcept {}

  std::array<std::stop_token, N> mStopTokens;
};

template <std::size_t N> struct WhenStopRequestedSender {

  auto operator co_await() noexcept -> WhenStopRequestedAwaiter<N> {
    return WhenStopRequestedAwaiter<N>{mStopTokens};
  }

  std::array<std::stop_token, N> mStopTokens;
};

template <class... StopTokens>
inline auto when_stop_requested(StopTokens... stopTokens)
    -> WhenStopRequestedSender<sizeof...(StopTokens)> {
  return WhenStopRequestedSender<sizeof...(StopTokens)>{
      std::array<std::stop_token, sizeof...(StopTokens)>{stopTokens...}};
}

template <class Fn> auto upon_stop_requested(Fn fn) -> Task<void> {
  co_await when_stop_requested();
  fn();
}

} // namespace cw