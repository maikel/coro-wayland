// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <coroutine>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <system_error>
#include <utility>
#include <variant>

namespace ms {

struct get_stop_token_t {};
inline constexpr get_stop_token_t get_stop_token{};

struct get_scheduler_t {};
inline constexpr get_scheduler_t get_scheduler{};

template <class Tp, class Context> class BasicTask;

template <class Tp, class Context> class TaskPromise;

template <class Tp, class Context> class BasicTask {
public:
  class Awaiter;
  using promise_type = TaskPromise<Tp, Context>;

  BasicTask(BasicTask&& other) noexcept : mHandle(std::exchange(other.mHandle, nullptr)) {}

  explicit BasicTask(std::coroutine_handle<promise_type> handle) noexcept : mHandle(handle) {}

  ~BasicTask() {
    if (mHandle) {
      mHandle.destroy();
    }
  }

  auto operator co_await() && -> Awaiter { return Awaiter{std::exchange(mHandle, nullptr)}; }

private:
  std::coroutine_handle<promise_type> mHandle;
};

template <class Tp, class Context> class BasicTask<Tp, Context>::Awaiter {
public:
  Awaiter() = delete;
  Awaiter(const Awaiter&) = delete;
  Awaiter(Awaiter&&) = delete;
  Awaiter& operator=(const Awaiter&) = delete;
  Awaiter& operator=(Awaiter&&) = delete;

  explicit Awaiter(std::coroutine_handle<promise_type> handle) noexcept;

  ~Awaiter();

  static constexpr auto await_ready() noexcept;

  template <class OtherPromise>
  auto await_suspend(std::coroutine_handle<OtherPromise> awaitingHandle) noexcept;

  auto await_resume() -> Tp;

private:
  std::coroutine_handle<promise_type> mHandle;
};

template <class Tp, class Context> class TaskPromiseBase {
public:
  TaskPromiseBase() = default;

  static constexpr auto initial_suspend() noexcept -> std::suspend_always;

  struct FinalAwaiter {
    static constexpr auto await_ready() noexcept -> std::true_type;

    template <class OtherPromise>
    auto await_suspend(std::coroutine_handle<OtherPromise> handle) -> std::coroutine_handle<>;

    void await_resume() noexcept;
  };
  static constexpr auto final_suspend() noexcept -> FinalAwaiter;

  void unhandled_exception();

  void unhandled_stopped();

  template <class OtherPromise>
  void set_continuation(std::coroutine_handle<OtherPromise> continuation);

  auto get_continuation() -> std::coroutine_handle<>;

  std::optional<Context> mContext;
};

template <class Tp, class Context> class TaskPromise : public TaskPromiseBase<Tp, Context> {
public:
  TaskPromise() = default;

  auto get_return_object() noexcept -> BasicTask<Tp, Context>;

  void return_value(Tp&& value) noexcept;

  void return_value(const Tp& value) noexcept;
};

template <class Context> class TaskPromise<void, Context> : public TaskPromiseBase<void, Context> {
public:
  TaskPromise() = default;

  auto get_return_object() noexcept -> BasicTask<void, Context>;

  void return_void() noexcept;
};

template <class Tp> class DefaultContext {
public:
  class Env {
  private:
    const DefaultContext* mContext;

  public:
    explicit Env(const DefaultContext* context) noexcept : mContext(context) {}

    auto query(get_stop_token_t) const noexcept -> std::stop_token {
      return mContext->mStopSource.get_token();
    }
  };

  template <class Promise> explicit DefaultContext(std::coroutine_handle<Promise> handle) noexcept;

  auto get_continuation() noexcept -> std::coroutine_handle<>;

  template <class... Args>
  void set_value(Args&&... args) noexcept;

  void set_error(std::exception_ptr eptr) noexcept;

  void set_stopped() noexcept;

  auto get_env() const noexcept -> Env;

private:
  struct OnStopRequested {
    DefaultContext& mContext;

    void operator()() noexcept { mContext.mStopSource.request_stop(); }
  };

  std::stop_source mStopSource;
  std::stop_callback<OnStopRequested> mStopCallback;
  std::variant<std::monostate, std::exception_ptr, Tp> mResult;
  std::coroutine_handle<> mContinuation;
  std::function<void()> mStopHandler;
};

template <class Tp> using Task = BasicTask<Tp, DefaultContext<Tp>>;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                             TaskPromiseBase<Tp, Context>

template <class Tp, class Context>
constexpr auto TaskPromiseBase<Tp, Context>::initial_suspend() noexcept -> std::suspend_always {
  return {};
}

template <class Tp, class Context>
constexpr auto TaskPromiseBase<Tp, Context>::final_suspend() noexcept -> FinalAwaiter {
  return {};
}

template <class Tp, class Context> void TaskPromiseBase<Tp, Context>::unhandled_exception() {
  mContext.value().set_error(std::current_exception());
}

template <class Tp, class Context> void TaskPromiseBase<Tp, Context>::unhandled_stopped() {
  mContext.value().set_stopped();
}

template <class Tp, class Context>
template <class OtherPromise>
void TaskPromiseBase<Tp, Context>::set_continuation(
    std::coroutine_handle<OtherPromise> continuation) {
  mContext.emplace(continuation);
}

template <class Tp, class Context>
auto TaskPromiseBase<Tp, Context>::get_continuation() -> std::coroutine_handle<> {
  return mContext.value().get_continuation();
}

template <class Tp, class Context>
constexpr auto TaskPromiseBase<Tp, Context>::FinalAwaiter::await_ready() noexcept
    -> std::true_type {
  return {};
}

template <class Tp, class Context>
template <class OtherPromise>
auto TaskPromiseBase<Tp, Context>::FinalAwaiter::await_suspend(
    std::coroutine_handle<OtherPromise> handle) -> std::coroutine_handle<> {
  return handle.promise().get_continuation();
}

template <class Tp, class Context>
void TaskPromiseBase<Tp, Context>::FinalAwaiter::await_resume() noexcept {}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                                 TaskPromise<Tp, Context>

template <class Tp, class Context>
auto TaskPromise<Tp, Context>::get_return_object() noexcept -> BasicTask<Tp, Context> {
  return BasicTask<Tp, Context>{
      std::coroutine_handle<TaskPromise<Tp, Context>>::from_promise(*this)};
}

template <class Tp, class Context>
void TaskPromise<Tp, Context>::return_value(Tp&& value) noexcept {
  this->mContext.value().set_value(std::move(value));
}

template <class Tp, class Context>
void TaskPromise<Tp, Context>::return_value(const Tp& value) noexcept {
  this->mContext.value().set_value(value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                                       DefaultContext<Tp>

template <class Tp>
template <class Promise>
DefaultContext<Tp>::DefaultContext(std::coroutine_handle<Promise> handle) noexcept
    : mContinuation(handle), mStopHandler{[handle]() noexcept {
        if constexpr (requires { handle.promise().unhandled_stopped(); }) {
          handle.promise().unhandled_stopped();
        } else {
          try {
            throw std::system_error(std::make_error_code(std::errc::operation_canceled));
          } catch (...) {
            handle.promise().unhandled_exception();
          }
        }
      }} {}

template <class Tp>
auto DefaultContext<Tp>::get_continuation() noexcept -> std::coroutine_handle<> {
  return mContinuation;
}

template <class Tp> 
template <class... Args>
void DefaultContext<Tp>::set_value(Args&&... args) noexcept {
  try {
    mResult.template emplace<2>(std::forward<Args>(args)...);
  } catch (...) {
    mResult.template emplace<1>(std::current_exception());
  }
}

template <class Tp> void DefaultContext<Tp>::set_error(std::exception_ptr eptr) noexcept {
  mResult.template emplace<1>(eptr);
}

template <class Tp> void DefaultContext<Tp>::set_stopped() noexcept { mStopHandler(); }

template <class Tp> auto DefaultContext<Tp>::get_env() const noexcept -> Env { return Env{this}; }

} // namespace ms