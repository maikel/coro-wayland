// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "AsyncScope.hpp"
#include "IoContext.hpp"

#include <unordered_map>

namespace ms {

template <class KeyT, class ValueT> class AsyncUnorderedMap {
public:
  auto emplace(KeyT key, ValueT value) -> Task<bool> {
    co_await mScheduler.schedule();
    auto [pos, inserted] = mMap.emplace(key, std::move(value));
    if (!inserted) {
      co_return false;
    }
    if (auto it = mWaiters.find(key); it != mWaiters.end()) {
      for (auto& handle : it->second) {
        handle.resume();
      }
      mWaiters.erase(it);
    }
    co_return true;
  }

  template <class KeyLikeT>
    requires std::constructible_from<KeyT, KeyLikeT>
  auto wait_for(const KeyLikeT& key) -> Task<ValueT> {
    co_await mScheduler.schedule();
    struct Awaiter {
      struct OnStopRequested {
        void operator()() noexcept try {
          mAwaiter->mMap->mScope.spawn(
              [](AsyncUnorderedMap* map, KeyT key,
                 std::coroutine_handle<TaskPromise<ValueT, TaskTraits>> handle) -> Task<void> {
                co_await map->mScheduler.schedule();
                auto it = map->mWaiters.find(key);
                if (it != map->mWaiters.end()) {
                  auto& waiters = it->second;
                  auto iter = std::find(waiters.begin(), waiters.end(), handle);
                  if (iter != waiters.end()) {
                    waiters.erase(iter);
                    if (waiters.empty()) {
                      map->mWaiters.erase(it);
                    }
                  }
                }
                handle.promise().unhandled_stopped();
              }(mAwaiter->mMap, mAwaiter->mKey, mAwaiter->mHandle));
        } catch (...) {
          // Swallow exceptions here
        }
        Awaiter* mAwaiter;
      };

      Awaiter(AsyncUnorderedMap* map, const KeyT& key) noexcept : mMap(map), mKey(key) {}

      AsyncUnorderedMap* mMap;
      KeyT mKey;
      std::coroutine_handle<TaskPromise<ValueT, TaskTraits>> mHandle;
      std::optional<std::stop_callback<OnStopRequested>> mStopCallback;

      bool await_ready() const noexcept { return mMap->mMap.find(mKey) != mMap->mMap.end(); }

      auto await_suspend(std::coroutine_handle<TaskPromise<ValueT, TaskTraits>> handle) noexcept
          -> std::coroutine_handle<> {
        if (mMap->mMap.find(mKey) == mMap->mMap.end()) {
          mHandle = handle;
          mMap->mWaiters[mKey].push_back(handle);
          std::stop_token stopToken = ms::get_stop_token(ms::get_env(handle.promise()));
          mStopCallback.emplace(stopToken, OnStopRequested{this});
          return std::noop_coroutine();
        } else {
          return handle;
        }
      }

      ValueT await_resume() {
        mStopCallback.reset();
        auto it = mMap->mMap.find(mKey);
        if (it != mMap->mMap.end()) {
          return it->second;
        } else {
          throw std::runtime_error("Key not found after await");
        }
      }
    };
    co_return co_await Awaiter{this, KeyT{key}};
  }

  auto close() -> Task<void> { co_await mScope.close(); }

  explicit AsyncUnorderedMap(IoScheduler scheduler) noexcept : mScheduler(scheduler) {}

private:
  IoScheduler mScheduler;
  AsyncScope mScope;
  std::unordered_map<KeyT, ValueT> mMap;
  std::unordered_map<KeyT, std::vector<std::coroutine_handle<TaskPromise<ValueT, TaskTraits>>>>
      mWaiters;
};

} // namespace ms