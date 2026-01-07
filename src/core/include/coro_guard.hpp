// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "ImmovableBase.hpp"
#include "Task.hpp"

namespace ms {
template <class AwaiterPromise> struct CoroGuardTask {
  struct promise_type;
  std::coroutine_handle<promise_type> mHandle;
};

template <class AwaiterPromise>
struct CoroGuardTask<AwaiterPromise>::promise_type : public ConnectablePromise {
  using ContinuationType = decltype(std::declval<AwaiterPromise&>().reset_continuation(
      std::declval<std::coroutine_handle<promise_type>>()));

  template <class Sender>
  explicit promise_type(AwaiterPromise& awaitingPromise, Sender&&)
      : mAwaitingPromise(awaitingPromise) {}

  auto get_return_object() noexcept -> CoroGuardTask {
    return CoroGuardTask{std::coroutine_handle<promise_type>::from_promise(*this)};
  }

  struct InitialSuspend {
    static constexpr auto await_ready() noexcept -> std::false_type { return {}; }

    auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept
        -> std::coroutine_handle<AwaiterPromise> {
      handle.promise().mContinuation = handle.promise().mAwaitingPromise.reset_continuation(handle);
      return std::coroutine_handle<AwaiterPromise>::from_promise(handle.promise().mAwaitingPromise);
    }

    void await_resume() noexcept {}
  };

  auto initial_suspend() noexcept -> InitialSuspend { return InitialSuspend{}; }

  struct FinalSuspend {
    static constexpr auto await_ready() noexcept -> std::false_type { return {}; }

    auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept
        -> std::coroutine_handle<> {
      if (handle.promise().mStopped) {
        handle.promise().mContinuation.set_stopped();
        handle.destroy();
        return std::noop_coroutine();
      }
      std::coroutine_handle<> cont = handle.promise().mContinuation.get_handle();
      handle.destroy();
      return cont;
    }

    void await_resume() noexcept {}
  };

  auto final_suspend() noexcept -> FinalSuspend { return FinalSuspend{}; }

  auto unhandled_exception() noexcept -> void { std::terminate(); }

  auto unhandled_stopped() noexcept -> void {
    mStopped = true;
    std::coroutine_handle<promise_type>::from_promise(*this).resume();
  }

  auto get_env() const noexcept { return mContinuation.get_env(); }

  AwaiterPromise& mAwaitingPromise;
  bool mStopped = false;
  ContinuationType mContinuation{};
};

template <class CleanupAwaitable> class CoroGuardAwaitable : ImmovableBase {
public:
  template <class Awaitable>
  explicit CoroGuardAwaitable(Awaitable&& cleanupAwaitable) noexcept
      : mCleanupAwaitable(std::forward<Awaitable>(cleanupAwaitable)) {}

  static constexpr auto await_ready() noexcept -> std::false_type { return {}; }

  template <class AwaiterPromise>
  auto await_suspend(std::coroutine_handle<AwaiterPromise> handle) noexcept -> void {
    [](AwaiterPromise&, CleanupAwaitable cleanupAwaitable) -> CoroGuardTask<AwaiterPromise> {
      co_await std::move(cleanupAwaitable);
    }(handle.promise(), std::move(mCleanupAwaitable));
  }

  void await_resume() noexcept {}

private:
  CleanupAwaitable mCleanupAwaitable;
};

template <class CleanupAwaitable>
inline auto coro_guard(CleanupAwaitable&& cleanupAwaitable) noexcept
    -> CoroGuardAwaitable<std::decay_t<CleanupAwaitable>> {
  return CoroGuardAwaitable<std::decay_t<CleanupAwaitable>>{
      std::forward<CleanupAwaitable>(cleanupAwaitable)};
}

} // namespace ms