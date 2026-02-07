// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "AsyncScope.hpp"
#include "IoTask.hpp"
#include "Observable.hpp"
#include "coro_guard.hpp"
#include "coro_just.hpp"
#include "read_env.hpp"

#include <atomic>
#include <coroutine>
#include <functional>
#include <utility>

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
  template <class Receiver> auto subscribe(Receiver receiver) const noexcept -> IoTask<void> {
    AsyncScope scope;
    co_await coro_guard(scope.close());
    const AsyncScopeHandle handle{scope};
    co_await receiver(coro_just(handle));
  }
};

auto create_scope() -> Observable<AsyncScopeHandle> { return AsyncScopeObservable{}; }

auto AsyncScope::make() -> Observable<AsyncScopeHandle> { return create_scope(); }

NestObservable::NestObservable(AsyncScope& scope) noexcept : mScope(&scope) {}

namespace {
auto nest_subscribe(AsyncScope& scope, std::function<auto(IoTask<void>)->IoTask<void>> receiver)
    -> IoTask<void> {
  co_await scope.nest(receiver(coro_just_void()));
}
} // namespace

auto NestObservable::subscribe(std::function<auto(IoTask<void>)->IoTask<void>> receiver)
    -> IoTask<void> {
  return nest_subscribe(*mScope, std::move(receiver));
}

auto AsyncScope::nest() -> NestObservable { return NestObservable{*this}; }

auto AsyncScopeHandle::nest() -> NestObservable { return NestObservable{*mScope}; }

auto StoppableScopeEnv::query(cw::get_scheduler_t) const noexcept -> IoScheduler {
  return mContext->mScheduler;
}

auto StoppableScopeEnv::query(cw::get_stop_token_t) const noexcept -> std::stop_token {
  return mContext->mStopSource.get_token();
}

auto StoppableScopeContext::get_env() const noexcept -> StoppableScopeEnv {
  return StoppableScopeEnv{this};
}

auto StoppableScope::make() -> Observable<StoppableScope> {
  struct StoppableScopeObservable {
    static auto subscribe(std::function<auto(IoTask<StoppableScope>)->IoTask<void>> receiver) noexcept
        -> IoTask<void> {
      const IoScheduler scheduler = co_await cw::read_env(cw::get_scheduler);
      StoppableScopeContext context{scheduler};
      co_await coro_guard(context.mScope.close());
      const StoppableScope scope{context};
      co_await receiver(coro_just(scope));
    }
  };
  return StoppableScopeObservable{};
}

} // namespace cw