// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "AsyncScope.hpp"
#include "ImmovableBase.hpp"
#include "IoTask.hpp"
#include "Observable.hpp"
#include "queries.hpp"
#include "read_env.hpp"

#include <cassert>
#include <queue>

namespace ms {

template <class Tp> class AsyncQueueHandle;

template <class Tp> class AsyncQueue : ImmovableBase {
public:
  explicit AsyncQueue(IoScheduler scheduler) noexcept;

  static auto make() -> Observable<AsyncQueueHandle<Tp>>;

  template <class... Args>
    requires std::constructible_from<Tp, Args...>
  auto push(Args&&... args);

  auto pop();

  auto close() noexcept -> Task<void>;

private:
  IoScheduler mScheduler;
  AsyncScope mScope;
  std::queue<Tp> mQueue;
  std::vector<std::coroutine_handle<TaskPromise<Tp, TaskTraits>>> mWaiters;
};

template <class Tp> class AsyncQueueHandle {
public:
  explicit AsyncQueueHandle(AsyncQueue<Tp>& queue) noexcept : mQueue(&queue) {}

  template <class... Args>
    requires std::constructible_from<Tp, Args...>
  auto push(Args&&... args) {
    return mQueue->push(std::forward<Args>(args)...);
  }

  auto pop() { return mQueue->pop(); }

private:
  AsyncQueue<Tp>* mQueue;
};

template <class Tp> class AsyncQueueObservable {
public:
  explicit AsyncQueueObservable(AsyncQueue<Tp>& queue) noexcept : mQueue(&queue) {}

  template <class Receiver> auto subscribe(Receiver receiver) const noexcept -> IoTask<void> {
    return [](AsyncQueue<Tp>* queue, Receiver receiver) -> IoTask<void> {
      std::stop_token stopToken = co_await ms::read_env(ms::get_stop_token);
      while (!stopToken.stop_requested()) {
        auto popTask = [](AsyncQueue<Tp>* queue) -> IoTask<Tp> {
          co_return co_await queue->pop();
        }(queue);
        co_await receiver(std::move(popTask));
      }
    }(mQueue, std::move(receiver));
  }

private:
  AsyncQueue<Tp>* mQueue;
};

template <class Tp> auto as_observable(AsyncQueue<Tp>& queue) noexcept -> AsyncQueueObservable<Tp> {
  return AsyncQueueObservable<Tp>(queue);
}

template <class Tp>
AsyncQueue<Tp>::AsyncQueue(IoScheduler scheduler) noexcept : mScheduler(std::move(scheduler)){};

template <class Tp>
template <class... Args>
  requires std::constructible_from<Tp, Args...>
auto AsyncQueue<Tp>::push(Args&&... args) {
  return mScope.nest([](AsyncQueue* queue, Args... args) -> Task<void> {
    co_await queue->mScheduler.schedule();
    queue->mQueue.push(Tp{std::move(args)...});
    if (!queue->mWaiters.empty()) {
      auto waiter = queue->mWaiters.front();
      queue->mWaiters.erase(queue->mWaiters.begin());
      waiter.resume();
    }
  }(this, std::forward<Args>(args)...));
}

template <class Tp> auto AsyncQueue<Tp>::pop() {
  return mScope.nest([](AsyncQueue* queue) -> Task<Tp> {
    co_await queue->mScheduler.schedule();
    struct Awaiter : ImmovableBase {
      struct OnStopRequested {
        void operator()() noexcept try {
          mAwaiter->mQueue->mScope.spawn(
              [](AsyncQueue* queue,
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

      Awaiter(AsyncQueue* queue) noexcept : mQueue(queue) {}

      AsyncQueue* mQueue;
      std::coroutine_handle<TaskPromise<Tp, TaskTraits>> mHandle;
      std::optional<std::stop_callback<OnStopRequested>> mStopCallback;
      bool await_ready() const noexcept { return false; }
      auto await_suspend(std::coroutine_handle<TaskPromise<Tp, TaskTraits>> handle) noexcept
          -> std::coroutine_handle<> {
        if (mQueue->mQueue.empty()) {
          mHandle = handle;
          mQueue->mWaiters.push_back(handle);
          std::stop_token stopToken = ms::get_stop_token(ms::get_env(handle.promise()));
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

template <class Tp> auto AsyncQueue<Tp>::close() noexcept -> Task<void> { co_await mScope.close(); }

template <class Tp> auto AsyncQueue<Tp>::make() -> Observable<AsyncQueueHandle<Tp>> {
  using Subscriber = std::function<auto(IoTask<AsyncQueueHandle<Tp>>)->IoTask<void>>;
  struct MakeObservable {
    auto subscribe(Subscriber subscriber) noexcept -> IoTask<void> {
      AsyncQueue<Tp> queue{co_await ms::read_env(ms::get_scheduler)};
      auto handleTask = [](AsyncQueue<Tp>* queue) -> IoTask<AsyncQueueHandle<Tp>> {
        co_return AsyncQueueHandle<Tp>{*queue};
      }(&queue);
      co_await subscriber(std::move(handleTask));
      co_await queue.close();
    }
  };
  return MakeObservable{};
}

} // namespace ms