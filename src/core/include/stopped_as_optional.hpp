// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "IoContext.hpp"
#include "IoTask.hpp"
#include "concepts.hpp"

#include <exception>
#include <optional>
#include <variant>

namespace cw {

template <class AwaiterPromise> struct StoppedAsOptionalStateBase {
  StoppedAsOptionalStateBase(std::coroutine_handle<AwaiterPromise> continuation)
      : mContinuation(continuation) {}

  std::atomic<int> mCompletionType{0}; // 0 = not completed, 1 = exception, 2 = value
  std::exception_ptr mException{nullptr};
  std::coroutine_handle<AwaiterPromise> mContinuation;
};

template <class AwaiterPromise, class ValueType>
struct StoppedAsOptionalState : StoppedAsOptionalStateBase<AwaiterPromise> {
  std::optional<typename ValueOrMonostateType<ValueType>::type> result;

  explicit StoppedAsOptionalState(std::coroutine_handle<AwaiterPromise> continuation)
      : StoppedAsOptionalStateBase<AwaiterPromise>{continuation} {}

  auto get_result() -> std::optional<typename ValueOrMonostateType<ValueType>::type> {
    int completionType = this->mCompletionType.load(std::memory_order_acquire);
    if (completionType == 1 && this->mException) {
      std::rethrow_exception(this->mException);
    } else {
      return std::move(result);
    }
  }
};

template <class AwaiterPromise> struct StoppedAsOptionalTask : ImmovableBase {
  struct promise_type : public ConnectablePromise {
    template <class Awaitable>
    explicit promise_type(StoppedAsOptionalStateBase<AwaiterPromise>* state, Awaitable&&)
        : mState(state) {}

    auto get_return_object() noexcept -> StoppedAsOptionalTask {
      return StoppedAsOptionalTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    auto initial_suspend() noexcept -> std::suspend_always { return {}; }

    struct FinalAwaiter {
      static constexpr auto await_ready() noexcept -> bool { return false; }

      auto await_suspend(std::coroutine_handle<promise_type> h) noexcept
          -> std::coroutine_handle<AwaiterPromise> {
        auto& promise = h.promise();
        return promise.mState->mContinuation;
      }

      void await_resume() noexcept {}
    };

    auto final_suspend() noexcept -> FinalAwaiter { return {}; }

    void return_void() noexcept {}

    void unhandled_exception() noexcept {
      mState->mException = std::current_exception();
      mState->mCompletionType.store(1, std::memory_order_release);
    }

    void unhandled_stopped() noexcept {
      // Treat stopped as no result
      mState->mContinuation.resume();
    }

    auto get_env() const noexcept -> cw::env_of_t<AwaiterPromise> {
      return cw::get_env(mState->mContinuation.promise());
    }

    StoppedAsOptionalStateBase<AwaiterPromise>* mState;
  };

  explicit StoppedAsOptionalTask(std::coroutine_handle<promise_type> handle) : mHandle(handle) {}

  ~StoppedAsOptionalTask() {
    if (mHandle) {
      mHandle.destroy();
    }
  }

  std::coroutine_handle<promise_type> mHandle;
};

template <class AwaiterPromise, class ChildSender> struct StoppedAsOptionalAwaiter {
  using ValueType =
      cw::await_result_t<ChildSender, typename StoppedAsOptionalTask<AwaiterPromise>::promise_type>;

  static constexpr auto makeTask =
      [](StoppedAsOptionalState<AwaiterPromise, ValueType>* state,
         ChildSender&& childSender) -> StoppedAsOptionalTask<AwaiterPromise> {
    if constexpr (std::is_void_v<ValueType>) {
      co_await std::move(childSender);
      state->result.emplace();
      state->mCompletionType.store(2, std::memory_order_release);
    } else {
      auto value = co_await std::move(childSender);
      state->result.emplace(std::move(value));
      state->mCompletionType.store(2, std::memory_order_release);
    }
  };

  explicit StoppedAsOptionalAwaiter(AwaiterPromise& promise, ChildSender&& childSender)
      : mState{std::coroutine_handle<AwaiterPromise>::from_promise(promise)},
        mChildTask(makeTask(&mState, std::move(childSender))) {}

  static constexpr auto await_ready() noexcept -> std::false_type { return {}; }

  auto await_suspend(std::coroutine_handle<AwaiterPromise>)
      -> std::coroutine_handle<typename StoppedAsOptionalTask<AwaiterPromise>::promise_type> {
    return mChildTask.mHandle;
  }

  auto await_resume() -> std::optional<typename ValueOrMonostateType<ValueType>::type> {
    return mState.get_result();
  }

  StoppedAsOptionalState<AwaiterPromise, ValueType> mState;
  StoppedAsOptionalTask<AwaiterPromise> mChildTask;
};

template <class ChildSender> struct StoppedAsOptionalSender {
  ChildSender mChildSender;

  template <class AwaiterPromise>
  auto
  connect(AwaiterPromise& awaiter) && -> StoppedAsOptionalAwaiter<AwaiterPromise, ChildSender> {
    return StoppedAsOptionalAwaiter<AwaiterPromise, ChildSender>{awaiter, std::move(mChildSender)};
  }
};

template <class ChildSender>
auto stopped_as_optional(ChildSender&& childSender)
    -> StoppedAsOptionalSender<std::remove_cvref_t<ChildSender>> {
  return StoppedAsOptionalSender<std::remove_cvref_t<ChildSender>>{
      std::forward<ChildSender>(childSender)};
}

} // namespace cw