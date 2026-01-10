// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "ImmovableBase.hpp"
#include "IoTask.hpp"
#include "Observable.hpp"

#include "concepts.hpp"

namespace cw {

template <class ValueT, class AwaiterPromise> struct UseResourceSharedState {
  explicit UseResourceSharedState(std::coroutine_handle<AwaiterPromise> awaiterHandle)
      : mAwaiterHandle(awaiterHandle),
        mContinuation(&IoTaskContextVtableFor<AwaiterPromise>, &awaiterHandle.promise()) {}

  std::coroutine_handle<AwaiterPromise> mAwaiterHandle;
  std::coroutine_handle<TaskPromise<void, IoTaskTraits>> mReleaseHandle{nullptr};
  std::optional<typename ValueOrMonostateType<ValueT>::type> mValue{};
  IoTaskContinuation mContinuation;
  bool mStopped{false};
};

template <class ValueT, class AwaiterPromise> struct UseResourceTask {
  class promise_type;
};

template <class ValueT, class AwaiterPromise>
class UseResourceTask<ValueT, AwaiterPromise>::promise_type : public ConnectablePromise {
public:
  template <class Sender>
  explicit promise_type(std::shared_ptr<UseResourceSharedState<ValueT, AwaiterPromise>> sharedState,
                        Sender&& /* subscribeSender */)
      : mSharedState(std::move(sharedState)) {}

  auto get_return_object() noexcept -> UseResourceTask { return UseResourceTask{}; }

  auto initial_suspend() -> std::suspend_never { return {}; }

  struct FinalSuspend {
    static constexpr auto await_ready() noexcept -> std::false_type { return {}; }
    auto await_suspend(std::coroutine_handle<promise_type> selfHandle) noexcept
        -> std::coroutine_handle<> {
      auto sharedState = selfHandle.promise().mSharedState;
      selfHandle.destroy();
      return sharedState->mContinuation.get_handle();
    }
    void await_resume() noexcept {}
  };
  auto final_suspend() noexcept -> FinalSuspend { return FinalSuspend{}; }

  void return_void() noexcept {}

  void unhandled_exception() noexcept { std::terminate(); }

  void unhandled_stopped() noexcept {
    mSharedState->mContinuation.set_stopped();
    std::coroutine_handle<promise_type>::from_promise(*this).destroy();
    // if (mSharedState->mReleaseHandle) {
    //   mSharedState->mReleaseHandle.resume();
    // }
  }

  auto get_env() const noexcept { return mSharedState->mContinuation.get_env(); }

private:
  std::shared_ptr<UseResourceSharedState<ValueT, AwaiterPromise>> mSharedState;
};

template <class ValueT, class AwaiterPromise> struct WaitUntilRelease {
  explicit WaitUntilRelease(
      std::shared_ptr<UseResourceSharedState<ValueT, AwaiterPromise>> sharedState) noexcept
      : mSharedState(std::move(sharedState)) {}

  static constexpr auto await_ready() noexcept -> std::false_type { return {}; }

  auto await_suspend(std::coroutine_handle<TaskPromise<void, IoTaskTraits>> handle) noexcept
      -> std::coroutine_handle<> {
    mSharedState->mReleaseHandle = handle;
    AwaiterPromise& awaiterPromise = mSharedState->mAwaiterHandle.promise();
    mSharedState->mContinuation = awaiterPromise.reset_continuation(handle);
    return mSharedState->mAwaiterHandle;
  }

  void await_resume() noexcept { mSharedState->mReleaseHandle = nullptr; }

  std::shared_ptr<UseResourceSharedState<ValueT, AwaiterPromise>> mSharedState;
};

template <class ValueT, class AwaiterPromise> struct UseResourceAwaiter : ImmovableBase {
  explicit UseResourceAwaiter(Observable<ValueT> observable) noexcept
      : mObservable(std::move(observable)) {}

  static constexpr auto await_ready() noexcept -> std::false_type { return {}; }

  auto await_suspend(std::coroutine_handle<AwaiterPromise> handle) noexcept -> void {
    mSharedState = std::make_shared<UseResourceSharedState<ValueT, AwaiterPromise>>(handle);
    auto receiver = [sharedState = mSharedState](IoTask<ValueT> task) -> IoTask<void> {
      auto s = sharedState;
      if constexpr (std::is_void_v<ValueT>) {
        co_await std::move(task);
        s->mValue.emplace();
      } else {
        s->mValue.emplace(co_await std::move(task));
      }
      co_await WaitUntilRelease(s);
    };
    auto subscribeTask = std::move(mObservable).subscribe(std::move(receiver));
    [](auto, IoTask<void> subscribeTask) -> UseResourceTask<ValueT, AwaiterPromise> {
      co_await std::move(subscribeTask);
    }(mSharedState, std::move(subscribeTask));
  }

  auto await_resume() noexcept -> typename ValueOrMonostateType<ValueT>::type {
    return std::move(mSharedState->mValue).value();
  }

  Observable<ValueT> mObservable;
  std::shared_ptr<UseResourceSharedState<ValueT, AwaiterPromise>> mSharedState;
};

template <class ValueT> struct UseResourceSender {
  explicit UseResourceSender(Observable<ValueT> observable) noexcept
      : mObservable(std::move(observable)) {}

  template <class AwaiterPromise> auto connect(AwaiterPromise&) && noexcept {
    return UseResourceAwaiter<ValueT, AwaiterPromise>{std::move(mObservable)};
  }

  Observable<ValueT> mObservable;
};

template <class ValueT>
auto use_resource(Observable<ValueT> observable) noexcept -> UseResourceSender<ValueT> {
  return UseResourceSender<ValueT>{std::move(observable)};
}

} // namespace cw