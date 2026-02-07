// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "AsyncScope.hpp"
#include "ImmovableBase.hpp"
#include "IoTask.hpp"
#include "Observable.hpp"
#include "coro_guard.hpp"
#include "coro_just.hpp"
#include "just_stopped.hpp"
#include "observables/use_resource.hpp"
#include "queries.hpp"
#include "read_env.hpp"
#include "stopped_as_optional.hpp"

#include <cassert>
#include <optional>

namespace cw {

template <class ValueT> class AsyncChannelContext;

template <class ValueT> class AsyncChannel {
public:
  AsyncChannel() = default;
  static auto make() -> Observable<AsyncChannel<ValueT>>;

  auto send(typename ValueOrMonostateType<ValueT>::type value) -> IoTask<void>;

  auto send() -> IoTask<void>
    requires std::is_void_v<ValueT>;

  auto receive() -> Observable<ValueT>;

private:
  explicit AsyncChannel(AsyncChannelContext<ValueT>& context) noexcept : mContext(&context) {}
  AsyncChannelContext<ValueT>* mContext;
};

template <class ValueT> class AsyncChannelContext : ImmovableBase {
public:
  explicit AsyncChannelContext(IoScheduler scheduler, AsyncScopeHandle scope) noexcept
      : mScheduler(scheduler), mScope(scope) {}

  IoScheduler mScheduler;
  AsyncScopeHandle mScope;
  std::optional<typename ValueOrMonostateType<ValueT>::type> mValue;
  std::coroutine_handle<TaskPromise<void, IoTaskTraits>> mContinuation;
};

template <class ValueT> auto AsyncChannel<ValueT>::make() -> Observable<AsyncChannel<ValueT>> {
  struct AsyncChannelObservable {
    auto subscribe(std::function<auto(IoTask<AsyncChannel<ValueT>>)->IoTask<void>> receiver)
        const noexcept -> IoTask<void> {
      IoScheduler scheduler = co_await cw::read_env(cw::get_scheduler);
      AsyncScopeHandle scope = co_await use_resource(AsyncScope::make());
      AsyncChannelContext<ValueT> context{scheduler, scope};
      AsyncChannel<ValueT> channel{context};
      co_await receiver(coro_just(channel));
    }
  };
  return AsyncChannelObservable{};
}

template <class ValueT>
auto AsyncChannel<ValueT>::send(typename ValueOrMonostateType<ValueT>::type value) -> IoTask<void> {
  co_await mContext->mScope.nest(
      [](AsyncChannel self, typename ValueOrMonostateType<ValueT>::type value) -> IoTask<void> {
        co_await self.mContext->mScheduler.schedule();
        if (self.mContext->mValue.has_value()) {
          throw std::runtime_error("AsyncChannel buffer overflow: value already present");
        } else {
          self.mContext->mValue.emplace(std::move(value));
        }
        struct SendAwaitable : ImmovableBase {
          struct OnStopRequested {
            void operator()() noexcept try {
              if (mAwaiter->mHandle) {
                mAwaiter->self.mContext->mScope.spawn(
                    [](AsyncChannel<ValueT> self,
                       std::coroutine_handle<TaskPromise<void, IoTaskTraits>> handle)
                        -> Task<void> {
                      co_await self.mContext->mScheduler.schedule();
                      if (self.mContext->mContinuation == handle) {
                        self.mContext->mValue.reset();
                        self.mContext->mContinuation = nullptr;
                        handle.promise().unhandled_stopped();
                      }
                    }(mAwaiter->self, mAwaiter->mHandle));
              }
            } catch (...) {
              // Swallow exceptions here
            }
            SendAwaitable* mAwaiter;
          };
          static constexpr auto await_ready() noexcept -> std::false_type { return {}; }
          auto await_suspend(std::coroutine_handle<TaskPromise<void, IoTaskTraits>> handle) noexcept
              -> std::coroutine_handle<> {
            mHandle = handle;
            mStopCallback.emplace(cw::get_stop_token(cw::get_env(handle.promise())),
                                  OnStopRequested{this});
            if (auto receiver = std::exchange(self.mContext->mContinuation, handle); receiver) {
              return receiver;
            }
            return std::noop_coroutine();
          }
          auto await_resume() noexcept -> void { mStopCallback.reset(); }
          AsyncChannel<ValueT> self;
          std::coroutine_handle<TaskPromise<void, IoTaskTraits>> mHandle;
          std::optional<std::stop_callback<OnStopRequested>> mStopCallback;
        };
        co_await SendAwaitable{{}, self, nullptr, std::nullopt};
      }(*this, std::move(value)));
}

template <class ValueT>
auto AsyncChannel<ValueT>::send() -> IoTask<void>
  requires std::is_void_v<ValueT>
{
  return send(std::monostate{});
}

template <class ValueT> auto AsyncChannel<ValueT>::receive() -> Observable<ValueT> {
  using Receiver = std::function<auto(IoTask<ValueT>)->IoTask<void>>;
  struct ReceiveObservable {
    AsyncChannel<ValueT> mChannel;

    static auto do_subscribe(AsyncChannel<ValueT> self, Receiver receiver) -> IoTask<void> {
      co_await self.mContext->mScheduler.schedule();
      while (true) {
        if (self.mContext->mValue.has_value()) {
          co_await [](AsyncChannel<ValueT> self, Receiver& receiver) -> IoTask<void> {
            auto value = [&] {
              if constexpr (std::is_void_v<ValueT>) {
                return coro_just_void();
              } else {
                return coro_just(std::move(self.mContext->mValue).value());
              }
            }();
            self.mContext->mValue.reset();
            auto sender = std::exchange(self.mContext->mContinuation, nullptr);
            co_await coro_guard([](auto sender, IoScheduler scheduler) -> IoTask<void> {
              co_await scheduler.schedule();
              sender.resume();
            }(sender, self.mContext->mScheduler));
            co_await receiver(std::move(value));
          }(self, receiver);
        } else {
          struct ReceiveAwaitable : ImmovableBase {
            struct OnStopRequested {
              void operator()() noexcept try {
                if (mAwaiter->mHandle) {
                  mAwaiter->self.mContext->mScope.spawn(
                      [](AsyncChannel<ValueT> self,
                         std::coroutine_handle<TaskPromise<void, IoTaskTraits>> handle)
                          -> Task<void> {
                        co_await self.mContext->mScheduler.schedule();
                        if (self.mContext->mContinuation == handle) {
                          self.mContext->mContinuation = nullptr;
                          handle.promise().unhandled_stopped();
                        }
                      }(mAwaiter->self, mAwaiter->mHandle));
                }
              } catch (...) {
                // Swallow exceptions here
              }
              ReceiveAwaitable* mAwaiter;
            };
            static constexpr auto await_ready() noexcept -> std::false_type { return {}; }
            auto
            await_suspend(std::coroutine_handle<TaskPromise<void, IoTaskTraits>> handle) noexcept
                -> void {
              mHandle = handle;
              self.mContext->mContinuation = handle;
              std::stop_token stopToken = cw::get_stop_token(cw::get_env(handle.promise()));
              mStopCallback.emplace(stopToken, OnStopRequested{this});
            }
            auto await_resume() noexcept -> void { mStopCallback.reset(); }
            AsyncChannel<ValueT> self;
            std::coroutine_handle<TaskPromise<void, IoTaskTraits>> mHandle;
            std::optional<std::stop_callback<OnStopRequested>> mStopCallback;
          };
          co_await ReceiveAwaitable{{}, self, nullptr, std::nullopt};
        }
      }
    }

    auto subscribe(Receiver receiver) const noexcept -> IoTask<void> {
      return do_subscribe(mChannel, std::move(receiver));
    }
  };
  return ReceiveObservable{*this};
}

} // namespace cw