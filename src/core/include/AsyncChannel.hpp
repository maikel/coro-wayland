// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "AsyncScope.hpp"
#include "ImmovableBase.hpp"
#include "IoTask.hpp"
#include "Observable.hpp"
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

  auto send(ValueT value) -> IoTask<void>;

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
  std::optional<ValueT> mValue;
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

template <class ValueT> auto AsyncChannel<ValueT>::send(ValueT value) -> IoTask<void> {
  co_await mContext->mScope.nest([](AsyncChannel self, ValueT value) -> IoTask<void> {
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
                   std::coroutine_handle<TaskPromise<void, IoTaskTraits>> handle) -> Task<void> {
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
        SendAwaitable* mAwaiter;
      };
      static constexpr auto await_ready() noexcept -> std::false_type { return {}; }
      auto await_suspend(std::coroutine_handle<TaskPromise<void, IoTaskTraits>> handle) noexcept
          -> std::coroutine_handle<> {
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

template <class ValueT> auto AsyncChannel<ValueT>::receive() -> Observable<ValueT> {
  using Receiver = std::function<auto(IoTask<ValueT>)->IoTask<void>>;
  struct ReceiveObservable {
    AsyncChannel<ValueT> mChannel;

    static auto do_subscribe(AsyncChannel<ValueT> self, Receiver receiver) -> IoTask<void> {
      co_await self.mContext->mScheduler.schedule();
      while (true) {
        if (self.mContext->mValue.has_value()) {
          ValueT value = std::move(self.mContext->mValue).value();
          self.mContext->mValue.reset();
          auto sender = std::exchange(self.mContext->mContinuation, nullptr);
          bool success = true;
          std::exception_ptr exception = nullptr;
          try {
            success = (co_await stopped_as_optional(receiver(coro_just<ValueT>(std::move(value)))))
                          .has_value();
          } catch (...) {
            exception = std::current_exception();
          }
          co_await self.mContext->mScheduler.schedule();
          if (!success || exception) {
            if (sender) {
              sender.promise().unhandled_stopped();
            }
            if (exception) {
              std::rethrow_exception(exception);
            }
            co_return;
          }
          sender.resume();
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