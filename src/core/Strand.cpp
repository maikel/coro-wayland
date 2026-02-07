// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Strand.hpp"

#include "AsyncScope.hpp"
#include "IoContext.hpp"
#include "IoTask.hpp"
#include "Observable.hpp"
#include "Task.hpp"
#include "coro_just.hpp"
#include "observables/use_resource.hpp"
#include "queries.hpp"
#include "read_env.hpp"

#include <coroutine>
#include <functional>
#include <queue>
#include <type_traits>
#include <utility>

namespace cw {

struct StrandContext {
  IoScheduler mScheduler;
  AsyncScopeHandle mScope;
  std::queue<std::coroutine_handle<>> mQueue;
};

auto Strand::make() -> Observable<Strand> {
  struct StrandObservable {
    static auto subscribe(std::function<auto(IoTask<Strand>)->IoTask<void>> receiver) noexcept
        -> IoTask<void> {
      const IoScheduler scheduler = co_await cw::read_env(cw::get_scheduler);
      const AsyncScopeHandle scope = co_await use_resource(AsyncScope::make());
      StrandContext context{.mScheduler = scheduler, .mScope = scope, .mQueue = {}};
      const Strand strand{context};
      co_await receiver(coro_just(strand));
    }
  };
  return StrandObservable{};
}

namespace {
auto lock_subscribe(StrandContext& context,
                    std::function<auto(IoTask<void>)->IoTask<void>> receiver) -> IoTask<void> {
  co_await context.mScheduler.schedule();
  if (context.mQueue.empty()) {
    context.mQueue.push(std::noop_coroutine());
  } else {
    struct Awaiter {
      static constexpr auto await_ready() noexcept -> std::false_type { return {}; }
      auto await_suspend(std::coroutine_handle<> handle) -> void { mContext.mQueue.push(handle); }
      auto await_resume() noexcept -> void {}
      StrandContext& mContext;
    };
    co_await Awaiter{context};
  }
  co_await receiver(coro_just_void());
  co_await context.mScheduler.schedule();
  context.mQueue.pop();
  if (!context.mQueue.empty()) {
    context.mScope.spawn([](StrandContext& context) -> Task<void> {
      co_await context.mScheduler.schedule();
      context.mQueue.front().resume();
    }(context));
  }
}
} // namespace

auto Strand::lock() -> Observable<void> {
  struct LockObservable {
    StrandContext* mContext;

    auto subscribe(std::function<auto(IoTask<void>)->IoTask<void>> receiver) const noexcept
        -> IoTask<void> {
      return lock_subscribe(*mContext, std::move(receiver));
    }
  };
  return LockObservable{mContext};
}

auto Strand::get_scheduler() const noexcept -> IoScheduler { return mContext->mScheduler; }

} // namespace cw