// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "ManualLifetime.hpp"
#include "Observable.hpp"
#include "bwos_lifo_queue.hpp"

#include <condition_variable>
#include <coroutine>
#include <mutex>
#include <ranges>
#include <thread>
#include <vector>

namespace cw {

struct BwosParams {
  std::size_t numBlocks;
  std::size_t blockSize;
};

struct WrokerThreadState;

template <class Fn> class BulkSender;

class StaticThreadPool {
public:
  StaticThreadPool(std::size_t numThreads, BwosParams params = BwosParams{8, 8});
  ~StaticThreadPool();

  void enqueue(std::coroutine_handle<> handle);

  template <class Iter, class Sentinel> void enqueue_bulk(Iter begin, Sentinel end);

  class ScheduleSender {
  public:
    explicit ScheduleSender(StaticThreadPool* pool) noexcept : mPool(pool) {}

    static constexpr auto await_ready() noexcept -> std::false_type { return {}; }
    auto await_suspend(std::coroutine_handle<> handle) noexcept -> void { mPool->enqueue(handle); }
    auto await_resume() noexcept -> void {}

  private:
    StaticThreadPool* mPool;
  };
  auto schedule() -> ScheduleSender { return ScheduleSender(this); }

  template <class Fn> auto schedule_bulk(std::size_t count, Fn fn) -> BulkSender<Fn>;

private:
  friend struct WrokerThreadState;
  std::vector<ManualLifetime<WrokerThreadState>> mWorkerThreads;
  std::mutex mMutex;
  std::condition_variable mCondition;
  std::vector<std::coroutine_handle<>> mTasks;
  std::size_t mThiefs{};
  std::size_t mSleeping{};
  bool mStopping = false;
};

template <class Iter, class Sentinel>
void StaticThreadPool::enqueue_bulk(Iter begin, Sentinel end) {
  {
    std::lock_guard lock(mMutex);
    mTasks.insert(mTasks.end(), begin, end);
  }
  mCondition.notify_all();
}

template <class Promise> struct BulkSharedState {
  explicit BulkSharedState(std::size_t& ongoingChildren, Promise& promise) noexcept
      : mPromise(promise), mOngoingChildren(ongoingChildren << 2) {}

  struct OnStopRequested {
    void operator()() noexcept { mSharedState->mStopSource.request_stop(); }
    BulkSharedState* mSharedState;
  };

  Promise& mPromise;
  std::exception_ptr mException;
  std::stop_source mStopSource;
  std::optional<std::stop_callback<OnStopRequested>> mStopCallback;
  alignas(64) std::atomic<std::size_t> mOngoingChildren;
};

template <class Promise> struct BulkTask {
  struct promise_type;

  explicit BulkTask(std::coroutine_handle<promise_type> handle) noexcept : mHandle(handle) {}

  BulkTask(const BulkTask& other) = delete;
  BulkTask& operator=(const BulkTask& other) = delete;

  BulkTask(BulkTask&& other) noexcept : mHandle(other.mHandle) { other.mHandle = nullptr; }

  BulkTask& operator=(BulkTask&& other) noexcept {
    if (this != &other) {
      if (mHandle) {
        mHandle.destroy();
      }
      mHandle = other.mHandle;
      other.mHandle = nullptr;
    }
    return *this;
  }

  ~BulkTask() {
    if (mHandle) {
      mHandle.destroy();
    }
  }

  std::coroutine_handle<promise_type> mHandle;
};

template <class Promise> struct BulkTask<Promise>::promise_type : ConnectablePromise {
  explicit promise_type(BulkSharedState<Promise>* sharedState, std::size_t) noexcept
      : mSharedState(sharedState) {}

  auto get_return_object() noexcept -> BulkTask {
    return BulkTask{std::coroutine_handle<promise_type>::from_promise(*this)};
  }

  static constexpr auto initial_suspend() noexcept -> std::suspend_always { return {}; }

  auto do_completion() noexcept -> void {
    std::size_t oldValue = mSharedState->mOngoingChildren.fetch_sub(0b100);
    std::size_t oldCount = oldValue >> 2;
    if (oldCount == 1) {
      mSharedState->mStopCallback.reset();
      if ((oldValue & 1) == 0) {
        std::coroutine_handle<Promise>::from_promise(mSharedState->mPromise).resume();
      } else {
        mSharedState->mPromise.unhandled_stopped();
      }
    }
  }

  struct FinalSuspender {
    static constexpr auto await_ready() noexcept -> std::false_type { return {}; }

    auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept -> void {
      handle.promise().do_completion();
    }

    void await_resume() noexcept {}
  };
  static constexpr auto final_suspend() noexcept -> FinalSuspender { return {}; }

  void return_void() noexcept {}

  void unhandled_exception() noexcept {
    std::size_t oldValue = mSharedState->mOngoingChildren.fetch_or(0b11);
    if ((oldValue & 0x10) == 0) {
      mSharedState->mException = std::current_exception();
    }
    if ((oldValue & 0x01) == 0) {
      mSharedState->mStopSource.request_stop();
    }
  }

  void unhandled_stopped() noexcept {
    std::size_t oldValue = mSharedState->mOngoingChildren.fetch_or(1);
    if ((oldValue & 0x01) == 0) {
      mSharedState->mStopSource.request_stop();
    }
    do_completion();
  }

  auto get_env() const noexcept -> env_of_t<Promise> { return cw::get_env(mSharedState->mPromise); }

  BulkSharedState<Promise>* mSharedState;
};

template <class Fn, class Promise> class BulkAwaitable : BulkSharedState<Promise> {
public:
  explicit BulkAwaitable(StaticThreadPool* pool, Fn fn, std::size_t nTasks,
                         Promise& promise) noexcept
      : BulkSharedState<Promise>(nTasks, promise), mPool(pool), mFn(std::move(fn)), mBulkTasks() {
    this->mBulkTasks.reserve(nTasks);
    for (std::size_t i = 0; i < nTasks; ++i) {
      this->mBulkTasks.emplace_back(
          [](BulkAwaitable<Fn, Promise>* sharedState, std::size_t index) -> BulkTask<Promise> {
            co_await sharedState->mFn(index);
          }(this, i));
    }
  }

  static constexpr auto await_ready() noexcept -> std::false_type { return {}; }

  auto await_resume() noexcept -> void {}

  auto await_suspend(std::coroutine_handle<>) noexcept -> void {
    this->mStopCallback.emplace(cw::get_stop_token(cw::get_env(this->mPromise)),
                                typename BulkSharedState<Promise>::OnStopRequested{this});
    if (this->mStopSource.stop_requested()) {
      this->mStopCallback.reset();
      this->mPromise.unhandled_stopped();
    } else {
      auto handles =
          std::views::transform(std::views::all(this->mBulkTasks), &BulkTask<Promise>::mHandle);
      this->mPool->enqueue_bulk(handles.begin(), handles.end());
    }
  }

private:
  StaticThreadPool* mPool;
  [[no_unique_address]] Fn mFn;
  std::vector<BulkTask<Promise>> mBulkTasks;
};

template <class Fn> class BulkSender {
public:
  explicit BulkSender(StaticThreadPool& pool, std::size_t count, Fn fn)
      : mPool(&pool), mCount(count), mFn(std::move(fn)) {}

  template <class Self, class Promise>
  auto connect(this Self&& self, Promise& promise) noexcept -> BulkAwaitable<Fn, Promise> {
    return BulkAwaitable<Fn, Promise>(self.mPool, std::forward_like<Self>(self.mFn), self.mCount,
                                      promise);
  }

private:
  StaticThreadPool* mPool;
  std::size_t mCount;
  [[no_unique_address]] Fn mFn;
};

template <class Fn>
auto StaticThreadPool::schedule_bulk(std::size_t count, Fn fn) -> BulkSender<Fn> {
  return BulkSender<Fn>(*this, count, std::move(fn));
}

} // namespace cw
