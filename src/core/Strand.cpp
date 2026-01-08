// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Strand.hpp"

#include "AsyncScope.hpp"
#include "coro_just.hpp"
#include "just_stopped.hpp"
#include "queries.hpp"
#include "read_env.hpp"
#include "stopped_as_optional.hpp"

#include <queue>

namespace ms {

struct StrandContext {
  IoScheduler mScheduler;
  AsyncScopeHandle mScope;
  std::queue<std::coroutine_handle<>> mQueue;
};

auto Strand::make() -> Observable<Strand> {
  struct StrandObservable {
    auto subscribe(std::function<auto(IoTask<Strand>)->IoTask<void>> receiver) const noexcept
        -> IoTask<void> {
      IoScheduler scheduler = co_await ms::read_env(ms::get_scheduler);
      AsyncScope scope{};
      StrandContext context{scheduler, AsyncScopeHandle{scope}, {}};
      Strand strand{context};
      std::exception_ptr exception = nullptr;
      bool stopped = false;
      try {
        stopped =
            (co_await ms::stopped_as_optional(receiver(coro_just<Strand>(strand)))).has_value();
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
  return StrandObservable{};
}

static auto lockSubscribe(StrandContext& context,
                          std::function<auto(IoTask<void>)->IoTask<void>> receiver)
    -> IoTask<void> {
  co_await context.mScheduler.schedule();
  if (context.mQueue.empty()) {
    context.mQueue.push(std::noop_coroutine());
  } else {
    struct Awaiter {
      static constexpr auto await_ready() noexcept -> std::false_type { return {}; }
      auto await_suspend(std::coroutine_handle<> handle) noexcept -> void {
        context.mQueue.push(handle);
      }
      auto await_resume() noexcept -> void {}
      StrandContext& context;
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

auto Strand::lock() -> Observable<void> {
  struct LockObservable {
    StrandContext* mContext;

    auto subscribe(std::function<auto(IoTask<void>)->IoTask<void>> receiver) const noexcept -> IoTask<void> {
      return lockSubscribe(*mContext, std::move(receiver));
    }
  };
  return LockObservable{mContext};
}

} // namespace ms