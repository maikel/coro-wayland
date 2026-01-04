// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "ImmovableBase.hpp"
#include "concepts.hpp"
#include "queries.hpp"

#include <atomic>
#include <coroutine>
#include <exception>
#include <optional>
#include <stop_token>
#include <system_error>
#include <tuple>
#include <variant>

namespace ms {

template <class AwaitingPromise, class... Senders> struct WhenAnySharedState {

  template <class Sender>
  using tupled_await_result_t =
      std::conditional_t<std::is_void_v<await_result_t<Sender, AwaitingPromise>>,
                         std::tuple<std::monostate>,
                         std::tuple<await_result_t<Sender, AwaitingPromise>>>;

  template <class... Tps>
  consteval static auto concat_tuples_to_one_variant(std::tuple<Tps>...) -> std::variant<Tps...>;

  template <class... Tuples>
  using variant_of_tuples_t = decltype(concat_tuples_to_one_variant(std::declval<Tuples>()...));

  std::atomic<std::ptrdiff_t> mRemainingOps = sizeof...(Senders);
  std::stop_source mStopSource;
  std::atomic<int> mResultType; // 0 = no completion, 1 = value, 2 = exception, 3 = stopped
  std::exception_ptr mException;
  std::optional<variant_of_tuples_t<tupled_await_result_t<Senders>...>> mResult;
  std::coroutine_handle<AwaitingPromise> mHandle;

  struct Env {
    const WhenAnySharedState* mState;

    template <class Qry>
      requires(callable<Qry, const env_of_t<AwaitingPromise>&>)
    auto query(Qry qry) const noexcept {
      return qry(ms::get_env(mState->mHandle.promise()));
    }

    auto query(ms::get_stop_token_t) const noexcept -> std::stop_token {
      return mState->mStopSource.get_token();
    }
  };

  WhenAnySharedState(AwaitingPromise& promise)
      : mHandle(std::coroutine_handle<AwaitingPromise>::from_promise(promise)) {}

  auto complete_promise() noexcept -> void {
    if (mRemainingOps.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      int resultType = mResultType.load(std::memory_order_acquire);
      if (resultType != 3) {
        mHandle.resume();
      } else if constexpr (requires { mHandle.promise().unhandled_stopped(); }) {
        mHandle.promise().unhandled_stopped();
      } else {
        mException = std::make_exception_ptr(
            std::system_error{std::make_error_code(std::errc::operation_canceled)});
        mHandle.resume();
      }
    }
  }

  template <std::size_t Ith, class... Args> auto notify_value(Args&&... args) noexcept -> void {
    int expected = 0;
    if (mResultType.compare_exchange_strong(expected, 1, std::memory_order_release,
                                            std::memory_order_relaxed)) {
      mResult.emplace(std::in_place_index<Ith>, std::forward<Args>(args)...);
      mStopSource.request_stop();
    }
  }

  auto notify_exception(std::exception_ptr exception) noexcept -> void {
    int expected = 0;
    if (mResultType.compare_exchange_strong(expected, 2, std::memory_order_release,
                                            std::memory_order_relaxed)) {
      mException = exception;
      mStopSource.request_stop();
    }
  }

  auto notify_stopped() noexcept -> void {
    int expected = 0;
    if (mResultType.compare_exchange_strong(expected, 3, std::memory_order_release,
                                            std::memory_order_relaxed)) {
      mStopSource.request_stop();
    }
    complete_promise();
  }

  auto get_env() const noexcept -> Env { return Env{this}; }

  auto get_results() {
    int resultType = mResultType.load();
    if (resultType == 2 && mException) {
      std::rethrow_exception(mException);
    } else {
      return mResult.value();
    }
  }
};

template <std::size_t Ith, class AwaitingPromise, class... Senders> struct WhenAnyChildTask;

template <std::size_t Ith, class AwaitingPromise, class... Senders>
struct WhenAnyChildPromise : ConnectablePromise {
  template <class ChildSender>
  explicit WhenAnyChildPromise(WhenAnySharedState<AwaitingPromise, Senders...>* state,
                               ChildSender&&)
      : mState(state) {}

  template <class Self>
  auto get_return_object(this Self& self) noexcept
      -> WhenAnyChildTask<Ith, AwaitingPromise, Senders...> {
    return WhenAnyChildTask<Ith, AwaitingPromise, Senders...>{
        std::coroutine_handle<Self>::from_promise(self)};
  }

  auto initial_suspend() noexcept -> std::suspend_always { return {}; }

  struct FinalSuspendAwaiter {
    static constexpr auto await_ready() noexcept -> std::false_type { return {}; }

    auto await_suspend(std::coroutine_handle<WhenAnyChildPromise> handle) noexcept -> void {
      handle.promise().mState->complete_promise();
    }

    void await_resume() noexcept {}
  };

  auto final_suspend() noexcept -> FinalSuspendAwaiter { return {}; }

  void unhandled_exception() noexcept { mState->notify_exception(std::current_exception()); }

  void unhandled_stopped() noexcept { mState->notify_stopped(); }

  void return_void() noexcept {}

  auto get_env() const noexcept { return mState->get_env(); }

  WhenAnySharedState<AwaitingPromise, Senders...>* mState;
};

template <std::size_t Ith, class AwaitingPromise, class... Senders> struct WhenAnyChildTask {
  using ChildSender = std::tuple_element_t<Ith, std::tuple<Senders...>>;

  using promise_type = WhenAnyChildPromise<Ith, AwaitingPromise, Senders...>;

  std::coroutine_handle<promise_type> mHandle;
};

template <class AwaitingPromise, class... Senders> struct WhenAnyAwaiter : private ImmovableBase {
  template <class... Sndrs, std::size_t... Is>
  static auto make_children_tuple(WhenAnySharedState<AwaitingPromise, Senders...>* sharedState,
                                  Sndrs&&... sndrs, std::index_sequence<Is...>) {
    return std::make_tuple(
        []<class ChildSndr>(
            WhenAnySharedState<AwaitingPromise, Senders...>* state,
            ChildSndr&& sender) -> WhenAnyChildTask<Is, AwaitingPromise, Senders...> {
          if constexpr (std::is_void_v<await_result_t<ChildSndr, AwaitingPromise>>) {
            co_await std::forward<ChildSndr>(sender);
            state->template notify_value<Is>();
          } else {
            auto result = co_await std::forward<ChildSndr>(sender);
            state->template notify_value<Is>(std::move(result));
          }
        }(sharedState, std::forward<Sndrs>(sndrs))...);
  }

  using ChildrenTuple = decltype(make_children_tuple<Senders...>(
      nullptr, std::declval<Senders>()..., std::index_sequence_for<Senders...>{}));

  WhenAnySharedState<AwaitingPromise, Senders...> mSharedState;
  ChildrenTuple mChildTasks;

  WhenAnyAwaiter(AwaitingPromise& promise, Senders&&... senders)
      : mSharedState(promise),
        mChildTasks(make_children_tuple<Senders...>(&mSharedState,
                                                    std::forward<Senders>(senders)...,
                                                    std::index_sequence_for<Senders...>{})) {}

  ~WhenAnyAwaiter() {
    std::apply([](auto&... childTasks) { (childTasks.mHandle.destroy(), ...); }, mChildTasks);
  }

  static constexpr auto await_ready() noexcept -> bool {
    if constexpr (sizeof...(Senders) == 0) {
      return true;
    } else {
      return false;
    }
  }

  auto await_suspend(std::coroutine_handle<AwaitingPromise>) noexcept -> void {
    std::apply([](auto&... childTasks) { (childTasks.mHandle.resume(), ...); }, mChildTasks);
  }
  auto await_resume() { return mSharedState.get_results(); }
};

template <class... Senders> struct WhenAnySender {
public:
  explicit WhenAnySender(Senders&&... senders) : mSenders(std::forward<Senders>(senders)...) {}

  template <class Self, class Promise>
  auto connect(this Self&& self, Promise& promise) noexcept -> WhenAnyAwaiter<Promise, Senders...> {
    return std::apply(
        [&promise](auto&&... senders) {
          return WhenAnyAwaiter<Promise, Senders...>(promise, std::forward_like<Self>(senders)...);
        },
        std::forward<Self>(self).mSenders);
  }

private:
  std::tuple<Senders...> mSenders;
};

template <std::movable... Senders>
auto when_any(Senders&&... senders) noexcept -> WhenAnySender<std::decay_t<Senders>...> {
  return WhenAnySender<std::decay_t<Senders>...>(std::forward<Senders>(senders)...);
}

} // namespace ms
