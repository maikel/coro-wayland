// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "AsyncScope.hpp"
#include "IoContext.hpp"
#include "queries.hpp"
#include "read_env.hpp"

#include <unordered_map>

namespace ms {

  template <class KeyT, class ValueT>
class AsyncUnorderedMapHandle;

template <class KeyT, class ValueT> class AsyncUnorderedMap : ImmovableBase {
public:
  static auto make() -> Observable<AsyncUnorderedMapHandle<KeyT, ValueT>>;

  auto emplace(KeyT key, ValueT value) {
    return mScope.nest([](AsyncUnorderedMap* self, KeyT key, ValueT value) -> Task<bool> {
      co_await self->mScheduler.schedule();
      auto [pos, inserted] = self->mMap.emplace(key, std::move(value));
      if (!inserted) {
        co_return false;
      }
      if (auto it = self->mWaiters.find(key); it != self->mWaiters.end()) {
        for (auto& handle : it->second) {
          handle.resume();
        }
        self->mWaiters.erase(it);
      }
      co_return true;
    }(this, std::move(key), std::move(value)));
  }

  template <class KeyLikeT>
    requires std::constructible_from<KeyT, KeyLikeT>
  auto wait_for(const KeyLikeT& key) {
    return mScope.nest([](AsyncUnorderedMap* self, const KeyLikeT& key) -> Task<ValueT> {
      co_await self->mScheduler.schedule();
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
      co_return co_await Awaiter{self, KeyT{key}};
    }(this, key));
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

template <class KeyT, class ValueT> class AsyncUnorderedMapHandle {
public:
  explicit AsyncUnorderedMapHandle(AsyncUnorderedMap<KeyT, ValueT>& map) noexcept : mMap(&map) {}

  template <class KeyLikeT>
    requires std::constructible_from<KeyT, KeyLikeT>
  auto wait_for(const KeyLikeT& key) const {
    return mMap->wait_for(key);
  }

  auto emplace(KeyT key, ValueT value) const {
    return mMap->emplace(std::move(key), std::move(value));
  }

private:
  AsyncUnorderedMap<KeyT, ValueT>* mMap;
};

template <class KeyT, class ValueT>
auto make_async_unordered_map() -> Observable<AsyncUnorderedMapHandle<KeyT, ValueT>> {
  struct AsyncUnorderedMapObservable {
    auto subscribe(std::function<auto(IoTask<AsyncUnorderedMapHandle<KeyT, ValueT>>)->IoTask<void>>
                       subscriber) noexcept -> IoTask<void> {
      IoScheduler scheduler = co_await ms::read_env(ms::get_scheduler);
      AsyncUnorderedMap<KeyT, ValueT> map{scheduler};
      auto task = [](AsyncUnorderedMap<KeyT, ValueT>* map)
          -> IoTask<AsyncUnorderedMapHandle<KeyT, ValueT>> {
        co_return AsyncUnorderedMapHandle<KeyT, ValueT>{*map};
      }(&map);
      co_await subscriber(std::move(task));
      co_await map.close();
    }
  };
  return Observable<AsyncUnorderedMapHandle<KeyT, ValueT>>{AsyncUnorderedMapObservable{}};
}

template <class KeyT, class ValueT>
auto AsyncUnorderedMap<KeyT, ValueT>::make()
    -> Observable<AsyncUnorderedMapHandle<KeyT, ValueT>> {
  return make_async_unordered_map<KeyT, ValueT>();
}

} // namespace ms