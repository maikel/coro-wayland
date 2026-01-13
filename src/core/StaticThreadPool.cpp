// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "StaticThreadPool.hpp"

#include "bwos_lifo_queue.hpp"

#include <ranges>
#include <algorithm>
#include <functional>
#include <random>

namespace cw {

struct WrokerThreadState {
  std::thread mThread;
  bwos::lifo_queue<std::coroutine_handle<>> mTaskQueue;
  std::vector<bwos::lifo_queue<std::coroutine_handle<>>*> mVictims;
  StaticThreadPool* mPool;
  std::mt19937 mRng{std::random_device{}()};

  explicit WrokerThreadState(BwosParams params, StaticThreadPool* pool) noexcept
      : mTaskQueue(params.numBlocks, params.blockSize), mPool(pool) {}

  void set_victims(const std::vector<bwos::lifo_queue<std::coroutine_handle<>>*>& victims) {
    for (auto* victim : victims) {
      if (victim != &mTaskQueue) {
        mVictims.push_back(victim);
      }
    }
  }

  auto try_pop_remote() -> bool;

  auto try_steal_task() noexcept -> std::coroutine_handle<>;

  void run() noexcept;

  ~WrokerThreadState() {
    if (mThread.joinable()) {
      mThread.join();
    }
  }
};

thread_local WrokerThreadState* tThisWorkerState = nullptr;

auto WrokerThreadState::try_pop_remote() -> bool {
  std::ptrdiff_t nTasks = static_cast<std::ptrdiff_t>(mPool->mTasks.size());
  if (nTasks > 0) {
    nTasks /= mPool->mWorkerThreads.size();
    auto maxCapacity =
        static_cast<std::ptrdiff_t>(mTaskQueue.block_size() * mTaskQueue.num_blocks());
    nTasks = std::clamp<std::ptrdiff_t>(nTasks, 1, maxCapacity);
    auto start = mPool->mTasks.end() - nTasks;
    auto iter = mTaskQueue.push_back(start, mPool->mTasks.end());
    mPool->mTasks.erase(start, iter);
    return true;
  }
  return false;
}

auto WrokerThreadState::try_steal_task() noexcept -> std::coroutine_handle<> {
  std::shuffle(mVictims.begin(), mVictims.end(), mRng);
  for (auto* victim : mVictims) {
    auto task = victim->steal_front();
    if (task) {
      return task;
    }
  }
  return nullptr;
}

void WrokerThreadState::run() noexcept {
  tThisWorkerState = this;
  while (true) {
    std::coroutine_handle<> task = mTaskQueue.pop_back();
    if (task) {
      task.resume();
      continue;
    }

    {
      std::lock_guard lock(mPool->mMutex);
      if (try_pop_remote()) {
        continue;
      }
      mPool->mThiefs++;
    }

    task = try_steal_task();
    if (task) {
        {
            std::lock_guard lock(mPool->mMutex);
            mPool->mThiefs--;
        }
        task.resume();
        continue;
    }

    {
      std::unique_lock lock(mPool->mMutex);
      mPool->mThiefs--;
      if (try_pop_remote()) {
        continue;
      }
      mPool->mSleeping++;
      if (mPool->mStopping) {
        return;
      }
      if (mPool->mThiefs == 0 && mPool->mSleeping < mPool->mWorkerThreads.size()) {
        // wake up another thread as there are still potential victims
        mPool->mCondition.notify_one();
      }
      mPool->mCondition.wait(lock);
      mPool->mSleeping--;
    }
  }
}

StaticThreadPool::StaticThreadPool(std::size_t numThreads, BwosParams params) {
  mWorkerThreads.resize(numThreads);
    for (std::size_t i = 0; i < numThreads; ++i) {
    mWorkerThreads[i].emplace(params, this);
  }
  std::vector<bwos::lifo_queue<std::coroutine_handle<>>*> queues;
  queues.reserve(numThreads);
  for (auto& worker : mWorkerThreads) {
    queues.push_back(&worker->mTaskQueue);
  }
  for (auto& worker : mWorkerThreads) {
    worker->set_victims(queues);
  }
  for (auto& worker : mWorkerThreads) {
    worker->mThread = std::thread(&WrokerThreadState::run, worker.get());
  }
}

StaticThreadPool::~StaticThreadPool() {
  {
    std::lock_guard lock(mMutex);
    mStopping = true;
    mCondition.notify_all();
  }
  for (auto& worker : mWorkerThreads) {
    if (worker->mThread.joinable()) {
      worker->mThread.join();
    }
  }
}

auto StaticThreadPool::enqueue(std::coroutine_handle<> handle) -> void {
  if (tThisWorkerState && tThisWorkerState->mTaskQueue.push_back(handle)) {
    return;
  }
  {
    std::lock_guard lock(mMutex);
    mTasks.push_back(handle);
  }
  mCondition.notify_one();
}

} // namespace cw