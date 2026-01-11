// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "AsyncScope.hpp"
#include "ImmovableBase.hpp"
#include "IoTask.hpp"
#include "Observable.hpp"
#include "coro_just.hpp"
#include "observables/use_resource.hpp"
#include "queries.hpp"
#include "read_env.hpp"

#include <cassert>
#include <queue>

namespace cw {

template <class Tp> class AsyncQueue;

template <class Tp> class AsyncQueueContext : ImmovableBase {
public:
  explicit AsyncQueueContext(IoScheduler scheduler, AsyncScopeHandle scope) noexcept;

  static auto make() -> Observable<AsyncQueue<Tp>>;

  template <class... Args>
    requires std::constructible_from<Tp, Args...>
  auto push(Args&&... args);

  auto pop();

private:
  IoScheduler mScheduler;
  AsyncScopeHandle mScope;
  std::queue<Tp> mQueue;
  std::vector<std::coroutine_handle<TaskPromise<Tp, TaskTraits>>> mWaiters;
};

template <class Tp> class AsyncQueueObservable {
public:
  explicit AsyncQueueObservable(AsyncQueueContext<Tp>& queue) noexcept : mQueue(&queue) {}

  template <class Receiver> auto subscribe(Receiver receiver) const noexcept -> IoTask<void> {
    return [](AsyncQueueContext<Tp>* queue, Receiver receiver) -> IoTask<void> {
      std::stop_token stopToken = co_await cw::read_env(cw::get_stop_token);
      while (!stopToken.stop_requested()) {
        auto popTask = [](AsyncQueueContext<Tp>* queue) -> IoTask<Tp> {
          co_return co_await queue->pop();
        }(queue);
        co_await receiver(std::move(popTask));
      }
    }(mQueue, std::move(receiver));
  }

private:
  AsyncQueueContext<Tp>* mQueue;
};

template <class Tp>
auto as_observable(AsyncQueueContext<Tp>& queue) noexcept -> AsyncQueueObservable<Tp> {
  return AsyncQueueObservable<Tp>(queue);
}

template <class Tp> class AsyncQueue {
public:
  explicit AsyncQueue(AsyncQueueContext<Tp>& queue) noexcept : mQueue(&queue) {}

  static auto make() -> Observable<AsyncQueue<Tp>>;

  template <class... Args>
    requires std::constructible_from<Tp, Args...>
  auto push(Args&&... args) {
    return mQueue->push(std::forward<Args>(args)...);
  }

  auto pop() { return mQueue->pop(); }

  auto observable() noexcept -> AsyncQueueObservable<Tp> {
    return AsyncQueueObservable<Tp>{*mQueue};
  }

private:
  AsyncQueueContext<Tp>* mQueue;
};

template <class Tp>
AsyncQueueContext<Tp>::AsyncQueueContext(IoScheduler scheduler, AsyncScopeHandle scope) noexcept
    : mScheduler(std::move(scheduler)), mScope(std::move(scope)){};

template <class Tp>
template <class... Args>
  requires std::constructible_from<Tp, Args...>
auto AsyncQueueContext<Tp>::push(Args&&... args) {
  return mScope.nest([](AsyncQueueContext* queue, Args... args) -> Task<void> {
    co_await queue->mScheduler.schedule();
    queue->mQueue.push(Tp{std::move(args)...});
    if (!queue->mWaiters.empty()) {
      auto waiter = queue->mWaiters.front();
      queue->mWaiters.erase(queue->mWaiters.begin());
      waiter.resume();
    }
  }(this, std::forward<Args>(args)...));
}

template <class Tp> auto AsyncQueueContext<Tp>::pop() {
  return mScope.nest([](AsyncQueueContext* queue) -> Task<Tp> {
    co_await queue->mScheduler.schedule();
    struct Awaiter : ImmovableBase {
      struct OnStopRequested {
        void operator()() noexcept try {
          mAwaiter->mQueue->mScope.spawn(
              [](AsyncQueueContext* queue,
                 std::coroutine_handle<TaskPromise<Tp, TaskTraits>> handle) -> Task<void> {
                co_await queue->mScheduler.schedule();
                auto iter = std::find(queue->mWaiters.begin(), queue->mWaiters.end(), handle);
                if (iter != queue->mWaiters.end()) {
                  queue->mWaiters.erase(iter);
                }
                handle.promise().unhandled_stopped();
              }(mAwaiter->mQueue, mAwaiter->mHandle));
        } catch (...) {
          // Swallow exceptions here
        }
        Awaiter* mAwaiter;
      };

      Awaiter(AsyncQueueContext* queue) noexcept : mQueue(queue) {}

      AsyncQueueContext* mQueue;
      std::coroutine_handle<TaskPromise<Tp, TaskTraits>> mHandle;
      std::optional<std::stop_callback<OnStopRequested>> mStopCallback;
      bool await_ready() const noexcept { return false; }
      auto await_suspend(std::coroutine_handle<TaskPromise<Tp, TaskTraits>> handle) noexcept
          -> std::coroutine_handle<> {
        if (mQueue->mQueue.empty()) {
          mHandle = handle;
          mQueue->mWaiters.push_back(handle);
          std::stop_token stopToken = cw::get_stop_token(cw::get_env(handle.promise()));
          mStopCallback.emplace(stopToken, OnStopRequested{this});
          return std::noop_coroutine();
        } else {
          return handle;
        }
      }
      Tp await_resume() {
        mStopCallback.reset();
        assert(!mQueue->mQueue.empty());
        Tp value = std::move(mQueue->mQueue.front());
        mQueue->mQueue.pop();
        return value;
      }
    };
    co_return co_await Awaiter{queue};
  }(this));
}

template <class Tp> auto AsyncQueueContext<Tp>::make() -> Observable<AsyncQueue<Tp>> {
  using Subscriber = std::function<auto(IoTask<AsyncQueue<Tp>>)->IoTask<void>>;
  struct MakeObservable {
    auto subscribe(Subscriber subscriber) noexcept -> IoTask<void> {
      IoScheduler scheduler = co_await cw::read_env(cw::get_scheduler);
      AsyncScopeHandle scope = co_await use_resource(AsyncScope::make());
      AsyncQueueContext<Tp> queue{scheduler, scope};
      AsyncQueue<Tp> handle{queue};
      co_await subscriber(coro_just(handle));
    }
  };
  return MakeObservable{};
}

template <class Tp> auto AsyncQueue<Tp>::make() -> Observable<AsyncQueue<Tp>> {
  return AsyncQueueContext<Tp>::make();
}

} // namespace cw