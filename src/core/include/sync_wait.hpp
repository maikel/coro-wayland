// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "IoContext.hpp"
#include "IoTask.hpp"
#include "concepts.hpp"

#include <exception>
#include <optional>
#include <variant>

namespace cw {

struct SyncWaitEnv {
  IoScheduler scheduler;

  auto query(get_scheduler_t) const noexcept -> IoScheduler { return scheduler; }
};

struct SyncWaitStateBase {
  IoContext* mContext;
  std::atomic<int> mCompletionType{0}; // 0 = not completed, 1 = exception, 2 = value
  std::exception_ptr mException{nullptr};
  auto get_env() const noexcept -> SyncWaitEnv { return SyncWaitEnv{mContext->get_scheduler()}; }
};

template <class ValueType> struct SyncWaitState : SyncWaitStateBase {
  std::optional<ValueType> result;

  explicit SyncWaitState(IoContext* ctx) : SyncWaitStateBase{ctx} {}

  auto get_result() -> std::optional<ValueType> {
    int completionType = mCompletionType.load(std::memory_order_acquire);
    if (completionType == 1 && mException) {
      std::rethrow_exception(mException);
    } else {
      return std::move(result);
    }
  }
};

template <> struct SyncWaitState<void> : SyncWaitStateBase {
  explicit SyncWaitState(IoContext* ctx) : SyncWaitStateBase{ctx} {}

  auto get_result() -> bool {
    int completionType = mCompletionType.load(std::memory_order_acquire);
    if (completionType == 1 && mException) {
      std::rethrow_exception(mException);
    }
    return mCompletionType.load(std::memory_order_acquire) == 2;
  }
};

struct SyncWaitTask {
  struct promise_type : public ConnectablePromise {
    template <class Awaitable>
    explicit promise_type(SyncWaitStateBase* state, Awaitable&&) : mState(state) {}

    auto get_return_object() noexcept -> SyncWaitTask {
      return SyncWaitTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    auto initial_suspend() noexcept -> std::suspend_never { return {}; }

    struct FinalSuspendAwaiter {
      static constexpr auto await_ready() noexcept -> std::false_type { return {}; }
      auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept -> void {
        handle.promise().mState->mContext->request_stop();
      }
      void await_resume() noexcept {}
    };
    auto final_suspend() noexcept -> FinalSuspendAwaiter { return {}; }

    void return_void() noexcept {}

    void unhandled_exception() noexcept {
      mState->mException = std::current_exception();
      mState->mCompletionType.store(1, std::memory_order_release);
    }

    void unhandled_stopped() noexcept {
      // Treat stopped as no result
      mState->mContext->request_stop();
    }

    auto get_env() const noexcept -> SyncWaitEnv { return mState->get_env(); }

    SyncWaitStateBase* mState;
  };

  ~SyncWaitTask() { mHandle.destroy(); }

  std::coroutine_handle<promise_type> mHandle;
};

template <class Awaitable> auto sync_wait(Awaitable&& awaitable) {
  using ValueType = cw::await_result_t<Awaitable, SyncWaitTask::promise_type>;
  IoContext ioContext;
  SyncWaitState<ValueType> state{&ioContext};
  auto task = [](SyncWaitState<ValueType>* state, Awaitable&& awaitable) -> SyncWaitTask {
    if constexpr (std::is_void_v<ValueType>) {
      co_await std::forward<Awaitable>(awaitable);
      state->mCompletionType.store(2, std::memory_order_release);
    } else {
      state->result.emplace(co_await std::forward<Awaitable>(awaitable));
      state->mCompletionType.store(2, std::memory_order_release);
    }
  }(&state, std::forward<Awaitable>(awaitable));
  ioContext.run();
  return state.get_result();
}

} // namespace cw