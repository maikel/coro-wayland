// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "AsyncScope.hpp"
#include "just_stopped.hpp"
#include "stopped_as_optional.hpp"

namespace ms {

auto AsyncScopeTask::promise_type::get_return_object() noexcept -> AsyncScopeTask {
  return AsyncScopeTask{};
}

auto AsyncScopeTask::promise_type::initial_suspend() noexcept -> std::suspend_never { return {}; }

auto AsyncScopeTask::promise_type::final_suspend() noexcept -> std::suspend_never { return {}; }

void AsyncScopeTask::promise_type::return_void() noexcept {
  if (mScope.mActiveTasks.fetch_sub(0b10, std::memory_order_acq_rel) == 0b10) {
    mScope.mWaitingHandle.resume();
  }
}

void AsyncScopeTask::promise_type::unhandled_exception() noexcept { return_void(); }

void AsyncScopeTask::promise_type::unhandled_stopped() noexcept {
  return_void();
  std::coroutine_handle<promise_type>::from_promise(*this).destroy();
}

auto AsyncScope::CloseAwaitable::await_suspend(std::coroutine_handle<> handle) noexcept
    -> std::coroutine_handle<> {
  mScope.mWaitingHandle = handle;
  if (mScope.mActiveTasks.fetch_sub(1, std::memory_order_acquire) == 1) {
    return handle;
  }
  return std::noop_coroutine();
}

void AsyncScope::CloseAwaitable::await_resume() noexcept {}

auto AsyncScope::close() noexcept -> CloseAwaitable { return CloseAwaitable{*this}; }

struct AsyncScopeObservable {
  AsyncScopeObservable() = default;

  template <class Receiver> auto subscribe(Receiver receiver) const noexcept -> IoTask<void> {
    AsyncScope scope;
    auto task = [](AsyncScope* scope) -> IoTask<AsyncScopeHandle> {
      co_return AsyncScopeHandle{*scope};
    }(&scope);
    bool stopped = false;
    std::exception_ptr exception = nullptr;
    try {
      stopped = (co_await ms::stopped_as_optional(receiver(std::move(task)))).has_value();
    } catch (...) {
      exception = std::current_exception();
    }
    co_await scope.close();
    if (stopped) {
      co_await ms::just_stopped();
    }
    if (exception) {
      std::rethrow_exception(exception);
    }
  }
};

auto create_scope() -> Observable<AsyncScopeHandle> { return AsyncScopeObservable{}; }

} // namespace ms