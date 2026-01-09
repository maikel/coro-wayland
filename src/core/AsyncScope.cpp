// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "AsyncScope.hpp"
#include "coro_just.hpp"
#include "just_stopped.hpp"
#include "stopped_as_optional.hpp"

namespace cw {

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
      stopped = (co_await cw::stopped_as_optional(receiver(std::move(task)))).has_value();
    } catch (...) {
      exception = std::current_exception();
    }
    co_await scope.close();
    if (stopped) {
      co_await cw::just_stopped();
    }
    if (exception) {
      std::rethrow_exception(exception);
    }
  }
};

auto create_scope() -> Observable<AsyncScopeHandle> { return AsyncScopeObservable{}; }

auto AsyncScope::make() -> Observable<AsyncScopeHandle> { return create_scope(); }

NestObservable::NestObservable(AsyncScope& scope) noexcept : mScope(&scope) {}

static auto nest_subscribe(AsyncScope& scope,
                           std::function<auto(IoTask<void>)->IoTask<void>> receiver)
    -> IoTask<void> {
  co_await scope.nest(receiver(coro_just_void()));
}

auto NestObservable::subscribe(std::function<auto(IoTask<void>)->IoTask<void>> receiver)
    -> IoTask<void> {
  return nest_subscribe(*mScope, std::move(receiver));
}

auto AsyncScope::nest() -> NestObservable { return NestObservable{*this}; }

auto AsyncScopeHandle::nest() -> NestObservable { return NestObservable{*mScope}; }

} // namespace cw