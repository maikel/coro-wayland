// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "ImmovableBase.hpp"
#include "Observable.hpp"

#include <atomic>
#include <coroutine>
#include <stdexcept>

namespace ms {

class AsyncScope;

struct ClosedScopeError : public std::runtime_error {
  ClosedScopeError() : std::runtime_error("AsyncScope is closed") {}
};

template <class Env> struct AsyncScopeTask {
  struct promise_type;
};

template <class Sender> class NestSender;

template <class Env> struct AsyncScopeTask<Env>::promise_type : ConnectablePromise {
  AsyncScope& mScope;
  Env mEnv;

  template <class Sender>
  promise_type(AsyncScope& scope, Env env, Sender&&) noexcept : mScope{scope}, mEnv{env} {}

  auto get_return_object() noexcept -> AsyncScopeTask<Env>;

  auto initial_suspend() noexcept -> std::suspend_never;

  auto final_suspend() noexcept -> std::suspend_never;

  void return_void() noexcept;

  void unhandled_exception() noexcept;

  void unhandled_stopped() noexcept;

  auto get_env() const noexcept -> Env { return mEnv; }
};

class AsyncScope;

class NestObservable {
public:
  explicit NestObservable(AsyncScope& scope) noexcept;

  auto subscribe(std::function<auto(IoTask<void>)->IoTask<void>> receiver) -> IoTask<void>;

private:
  AsyncScope* mScope;
};

class AsyncScopeHandle {
public:
  explicit AsyncScopeHandle(AsyncScope& scope) noexcept : mScope(&scope) {}

  template <class Sender, class Env = EmptyEnv> void spawn(Sender&& sender, Env env = Env());

  template <class Sender> auto nest(Sender&& sender) -> NestSender<Sender>;

  auto nest() -> NestObservable;

private:
  AsyncScope* mScope;
};

template <class AwaiterPromise> struct NestTask {
  class promise_type;

  std::coroutine_handle<promise_type> mHandle;
};
class AsyncScope : ImmovableBase {
public:
  AsyncScope() = default;

  static auto make() -> Observable<AsyncScopeHandle>;

  template <class Sender, class Env = EmptyEnv> void spawn(Sender&& sender, Env env = Env()) {
    std::ptrdiff_t expected = 1;
    while (!mActiveTasks.compare_exchange_weak(expected, expected + 0b10, std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
      if ((expected & 0b01) == 0) {
        throw std::runtime_error("Cannot spawn new tasks on a stopped AsyncScope");
      }
    }
    [](AsyncScope&, Env, Sender sndr) -> AsyncScopeTask<Env> {
      co_return co_await std::forward<Sender>(sndr);
    }(*this, env, std::forward<Sender>(sender));
  }

  template <class Sender> auto nest(Sender&& sender) -> NestSender<Sender>;

  auto nest() -> NestObservable;

  struct CloseAwaitable;

  auto close() noexcept -> CloseAwaitable;

  template <class AwaiterPromise> struct NestAwaitableBase;

private:
  template <class Env> friend struct AsyncScopeTask;
  std::atomic<std::ptrdiff_t> mActiveTasks{1};
  std::coroutine_handle<> mWaitingHandle;
};

struct AsyncScope::CloseAwaitable {
  AsyncScope& mScope;

  static constexpr auto await_ready() noexcept -> std::false_type { return {}; }

  auto await_suspend(std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>;

  void await_resume() noexcept;
};

template <class Env>
auto AsyncScopeTask<Env>::promise_type::get_return_object() noexcept -> AsyncScopeTask<Env> {
  return AsyncScopeTask<Env>{};
}

template <class Env>
auto AsyncScopeTask<Env>::promise_type::initial_suspend() noexcept -> std::suspend_never {
  return {};
}

template <class Env>
auto AsyncScopeTask<Env>::promise_type::final_suspend() noexcept -> std::suspend_never {
  return {};
}

template <class Env> void AsyncScopeTask<Env>::promise_type::return_void() noexcept {
  if (mScope.mActiveTasks.fetch_sub(0b10, std::memory_order_acq_rel) == 0b10) {
    mScope.mWaitingHandle.resume();
  }
}

template <class Env> void AsyncScopeTask<Env>::promise_type::unhandled_exception() noexcept {
  return_void();
}

template <class Env> void AsyncScopeTask<Env>::promise_type::unhandled_stopped() noexcept {
  return_void();
  std::coroutine_handle<promise_type>::from_promise(*this).destroy();
}

auto create_scope() -> Observable<AsyncScopeHandle>;

template <class Sender, class Env> void AsyncScopeHandle::spawn(Sender&& sender, Env env) {
  mScope->spawn(std::forward<Sender>(sender), env);
}

template <class AwaiterPromise> struct AsyncScope::NestAwaitableBase : ImmovableBase {
  explicit NestAwaitableBase(AsyncScope& scope) noexcept : mScope(scope) {}

  AsyncScope& mScope;
  std::coroutine_handle<AwaiterPromise> mWaitingParent{nullptr};
  std::exception_ptr mException{nullptr};

  void try_increase_ref_count() {
    std::ptrdiff_t expected = 1;
    while (!mScope.mActiveTasks.compare_exchange_weak(
        expected, expected + 0b10, std::memory_order_acq_rel, std::memory_order_acquire)) {
      if ((expected & 0b01) == 0) {
        throw ClosedScopeError();
      }
    }
  }

  auto notify_completion() -> std::coroutine_handle<> {
    std::ptrdiff_t count = mScope.mActiveTasks.fetch_sub(0b10, std::memory_order_acq_rel);
    if (count == 0b10) {
      return mScope.mWaitingHandle;
    } else {
      return std::noop_coroutine();
    }
  }
};

template <class AwaiterPromise> class NestTask<AwaiterPromise>::promise_type {
public:
  template <class Sender>
  explicit promise_type(AsyncScope::NestAwaitableBase<AwaiterPromise>* opState, Sender&&) noexcept
      : mOperationState(opState) {}

  auto get_return_object() noexcept -> NestTask {
    return NestTask{std::coroutine_handle<promise_type>::from_promise(*this)};
  }

  auto initial_suspend() noexcept -> std::suspend_always { return {}; }

  struct FinalSuspend {
    static constexpr auto await_ready() noexcept -> std::false_type { return {}; }

    auto await_suspend(std::coroutine_handle<promise_type> selfHandle) noexcept
        -> std::coroutine_handle<> {
      auto* opState = selfHandle.promise().mOperationState;
      auto closedAwaiter = opState->notify_completion();
      closedAwaiter.resume();
      return opState->mWaitingParent;
    }

    void await_resume() noexcept {}
  };

  auto final_suspend() noexcept -> FinalSuspend { return {}; }

  void return_void() noexcept {}

  void unhandled_exception() noexcept { mOperationState->mException = std::current_exception(); }

  void unhandled_stopped() noexcept {
    auto parent = mOperationState->mWaitingParent;
    mOperationState->notify_completion().resume();
    parent.promise().unhandled_stopped();
  }

  auto get_env() const noexcept -> ms::env_of_t<AwaiterPromise> {
    return ms::get_env(mOperationState->mWaitingParent.promise());
  }

private:
  AsyncScope::NestAwaitableBase<AwaiterPromise>* mOperationState;
};

template <class Sender, class AwaiterPromise>
struct NestAwaitable : AsyncScope::NestAwaitableBase<AwaiterPromise> {

  explicit NestAwaitable(Sender&& sender, AsyncScope& scope) noexcept
      : AsyncScope::NestAwaitableBase<AwaiterPromise>{scope},
        mSender(std::forward<Sender>(sender)) {}

  ~NestAwaitable() {
    if (this->mChildTask) {
      this->mChildTask.destroy();
    }
  }

  using ValueT = ms::await_result_t<Sender, typename NestTask<AwaiterPromise>::promise_type>;

  using ValueOrMonostate = std::conditional_t<std::is_void_v<ValueT>, std::monostate, ValueT>;

  static constexpr auto await_ready() noexcept -> std::false_type { return {}; }

  auto await_suspend(std::coroutine_handle<AwaiterPromise> awaitingHandle) noexcept
      -> std::coroutine_handle<> {
    this->mWaitingParent = awaitingHandle;
    try {
      this->try_increase_ref_count();
    } catch (...) {
      this->mException = std::current_exception();
      return awaitingHandle;
    }
    this->mChildTask = [](NestAwaitable* nester, Sender sndr) -> NestTask<AwaiterPromise> {
      if constexpr (std::is_void_v<ValueT>) {
        co_await std::move(sndr);
        nester->mValue.emplace();
      } else {
        nester->mValue.emplace(co_await std::move(sndr));
      }
    }(this, std::forward<Sender>(mSender))
                                                                     .mHandle;
    return this->mChildTask;
  }

  auto await_resume() -> ValueT {
    if (this->mException) {
      std::rethrow_exception(this->mException);
    }
    if constexpr (!std::is_void_v<ValueT>) {
      return this->mValue.value();
    }
  }

  Sender mSender;
  std::optional<ValueOrMonostate> mValue;
  std::coroutine_handle<typename NestTask<AwaiterPromise>::promise_type> mChildTask{nullptr};
};

template <class Sender> class NestSender {
public:
  template <class Sndr>
  explicit NestSender(Sndr&& sender, AsyncScope& scope) noexcept
      : mSender(std::forward<Sndr>(sender)), mScope(&scope) {}

  template <class AwaiterPromise>
  auto connect(AwaiterPromise&) && noexcept -> NestAwaitable<Sender, AwaiterPromise> {
    return NestAwaitable<Sender, AwaiterPromise>{std::move(mSender), *mScope};
  }

private:
  Sender mSender;
  AsyncScope* mScope;
};

template <class Sender> auto AsyncScope::nest(Sender&& sender) -> NestSender<Sender> {
  return NestSender<Sender>{std::forward<Sender>(sender), *this};
}

template <class Sender> auto AsyncScopeHandle::nest(Sender&& sender) -> NestSender<Sender> {
  return NestSender<Sender>{std::forward<Sender>(sender), *mScope};
}

} // namespace ms