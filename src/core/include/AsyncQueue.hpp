// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "IoContext.hpp"
#include "Task.hpp"
#include "queries.hpp"

#include <cassert>
#include <queue>

namespace ms {

template <class Tp> class AsyncQueue {
public:
  AsyncQueue(const AsyncQueue&) = delete;
  AsyncQueue& operator=(const AsyncQueue&) = delete;
  AsyncQueue(AsyncQueue&&) = delete;
  AsyncQueue& operator=(AsyncQueue&&) = delete;
  ~AsyncQueue() = default;

  explicit AsyncQueue(IoScheduler scheduler) noexcept;

  auto push(const Tp& value) -> Task<void>;

  auto push(Tp&& value) -> Task<void>;

  auto pop() -> Task<Tp>;

private:
  IoScheduler mScheduler;
  std::queue<Tp> mQueue;
  std::queue<std::coroutine_handle<>> mWaiters;
};

template <class Tp>
AsyncQueue<Tp>::AsyncQueue(IoScheduler scheduler) noexcept : mScheduler(std::move(scheduler)){};

template <class Tp> auto AsyncQueue<Tp>::push(const Tp& value) -> Task<void> {
  co_await mScheduler.schedule();
  if (!mWaiters.empty()) {
    auto waiter = mWaiters.front();
    mWaiters.pop();
    waiter.resume();
  } else {
    mQueue.push(value);
  }
}

template <class Tp> auto AsyncQueue<Tp>::push(Tp&& value) -> Task<void> {
  co_await mScheduler.schedule();
  if (!mWaiters.empty()) {
    assert(mQueue.empty());
    auto waiter = mWaiters.front();
    mWaiters.pop();
    waiter.resume();
  } else {
    mQueue.push(std::move(value));
  }
}

template <class Tp> auto AsyncQueue<Tp>::pop() -> Task<Tp> {
  co_await mScheduler.schedule();
  if (mQueue.empty()) {
    struct Awaiter {
      AsyncQueue* mQueue;
      bool await_ready() const noexcept { return false; }
      void await_suspend(std::coroutine_handle<> handle) noexcept { mQueue->mWaiters.push(handle); }
      Tp await_resume() {
        assert(!mQueue->mQueue.empty());
        Tp value = std::move(mQueue->mQueue.front());
        mQueue->mQueue.pop();
        return value;
      }
    };
    co_return co_await Awaiter{this};
  } else {
    Tp value = std::move(mQueue.front());
    mQueue.pop();
    co_return value;
  }
}

} // namespace ms