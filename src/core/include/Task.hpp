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

struct empty_env {};

struct get_env_t {
  template <class Promise> auto operator()(const Promise& promise) const noexcept {
    if constexpr (requires { promise.get_env(); }) {
      return promise.get_env();
    } else {
      return empty_env{};
    }
  }
};
inline constexpr get_env_t get_env{};

struct get_stop_token_t {
  template <class Env> auto operator()(const Env& env) const noexcept -> std::stop_token {
    if constexpr (requires { env.query(*this); }) {
      return env.query(*this);
    } else {
      return std::stop_token{};
    }
  }
};
inline constexpr get_stop_token_t get_stop_token{};

struct get_scheduler_t {};
inline constexpr get_scheduler_t get_scheduler{};

template <class Tp, class Context> class BasicTask;

template <class Tp, class Context> class TaskPromise;

/// A lazily-evaluated coroutine task that produces a value of type Tp.
///
/// @tparam Tp The result type of the task (use void for no result)
/// @tparam Context The execution context managing task lifecycle and cancellation
///
/// BasicTask represents a coroutine that doesn't start executing until co_await'ed.
/// It supports automatic continuation chaining and cancellation via stop tokens.
/// Use the Task<Tp> alias for the default context with cancellation support.
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

/// Awaiter for BasicTask that handles suspension, continuation, and result propagation.
///
/// When a task is co_await'ed, this awaiter:
/// 1. Sets up the continuation chain to resume the awaiting coroutine
/// 2. Starts execution of the task (via symmetric transfer)
/// 3. Propagates the result or exception when the task completes
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
  auto await_suspend(std::coroutine_handle<OtherPromise> awaitingHandle) noexcept
      -> std::coroutine_handle<>;

  auto await_resume() -> Tp;

private:
  std::coroutine_handle<promise_type> mHandle;
};

/// Base promise type for task coroutines, handling lifecycle and continuation management.
///
/// Provides common functionality for suspending at start, chaining continuations,
/// and propagating exceptions/cancellations through the context.
template <class Tp, class Context> class TaskPromiseBase {
public:
  TaskPromiseBase() = default;

  /// Suspend at the start so task only executes when co_await'ed
  static constexpr auto initial_suspend() noexcept -> std::suspend_always;

  /// Final awaiter that implements symmetric transfer for continuation chaining.
  /// When the task completes, this resumes the awaiting coroutine without stack growth.
  struct FinalAwaiter {
    static constexpr auto await_ready() noexcept -> std::true_type;

    /// Returns the continuation to resume, enabling tail-call optimization
    template <class OtherPromise>
    auto await_suspend(std::coroutine_handle<OtherPromise> handle) -> std::coroutine_handle<>;

    void await_resume() noexcept;
  };
  static constexpr auto final_suspend() noexcept -> FinalAwaiter;

  void unhandled_stopped();

  template <class OtherPromise>
  void set_continuation(std::coroutine_handle<OtherPromise> continuation);

  auto get_continuation() -> std::coroutine_handle<>;

  auto get_env() const noexcept -> typename Context::env_type;

private:
  std::optional<Context> mContext;
  std::coroutine_handle<> mContinuation;
  std::function<void()> mStopHandler;
};

/// Promise type for tasks returning a value of type Tp
template <class Tp, class Context> class TaskPromise : public TaskPromiseBase<Tp, Context> {
public:
  TaskPromise() = default;

  auto get_return_object() noexcept -> BasicTask<Tp, Context>;

  void unhandled_exception();

  void return_value(Tp&& value) noexcept;

  void return_value(const Tp& value) noexcept;

  std::variant<std::monostate, std::exception_ptr, Tp> mResult;
};

/// Promise type specialization for tasks returning void
template <class Context> class TaskPromise<void, Context> : public TaskPromiseBase<void, Context> {
public:
  TaskPromise() = default;

  auto get_return_object() noexcept -> BasicTask<void, Context>;

  void unhandled_exception();

  void return_void() noexcept;

  struct Unit {};
  std::variant<std::monostate, std::exception_ptr, Unit> mResult;
};

/// Default execution context providing cancellation support via stop tokens.
///
/// Manages:
/// - Stop token propagation from parent coroutine
/// - Automatic cancellation via stop_callback
/// - Environment query interface for child tasks
template <class Tp> class DefaultContext {
public:
  /// Environment object providing query interface for stop tokens
  class Env {
  private:
    const DefaultContext* mContext;

  public:
    explicit Env(const DefaultContext* context) noexcept : mContext(context) {}

    auto query(get_stop_token_t) const noexcept -> std::stop_token {
      return mContext->mStopSource.get_token();
    }
  };

  using env_type = Env;

  template <class Promise> explicit DefaultContext(std::coroutine_handle<Promise> handle) noexcept;

  template <class... Args> void set_value(Args&&... args) noexcept;

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
};

/// Convenient alias for BasicTask with default cancellation support
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

template <class Tp, class Context> void TaskPromiseBase<Tp, Context>::unhandled_stopped() {
  mStopHandler();
}

/// Store the awaiting coroutine as continuation and set up stop handler.
/// If parent supports unhandled_stopped(), use it; otherwise convert to exception.
template <class Tp, class Context>
template <class OtherPromise>
void TaskPromiseBase<Tp, Context>::set_continuation(
    std::coroutine_handle<OtherPromise> continuation) {
  mContext.emplace(continuation);
  mContinuation = continuation;
  mStopHandler = [continuation]() noexcept {
    if constexpr (requires { continuation.promise().unhandled_stopped(); }) {
      continuation.promise().unhandled_stopped();
    } else {
      try {
        throw std::system_error(std::make_error_code(std::errc::operation_canceled));
      } catch (...) {
        continuation.promise().unhandled_exception();
      }
    }
  };
}

template <class Tp, class Context>
auto TaskPromiseBase<Tp, Context>::get_env() const noexcept -> typename Context::env_type {
  return mContext->get_env();
}

template <class Tp, class Context>
auto TaskPromiseBase<Tp, Context>::get_continuation() -> std::coroutine_handle<> {
  return mContinuation;
}

template <class Tp, class Context>
constexpr auto TaskPromiseBase<Tp, Context>::FinalAwaiter::await_ready() noexcept
    -> std::true_type {
  return {};
}

/// Implement symmetric transfer: return continuation instead of resuming directly.
/// This prevents unbounded stack growth in long continuation chains.
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

template <class Tp, class Context> void TaskPromise<Tp, Context>::unhandled_exception() {
  mResult.template emplace<1>(std::current_exception());
}

template <class Tp, class Context>
void TaskPromise<Tp, Context>::return_value(Tp&& value) noexcept {
  mResult.template emplace<2>(std::move(value));
}

template <class Tp, class Context>
void TaskPromise<Tp, Context>::return_value(const Tp& value) noexcept {
  mResult.template emplace<2>(value);
}

template <class Context>
auto TaskPromise<void, Context>::get_return_object() noexcept -> BasicTask<void, Context> {
  return BasicTask<void, Context>{
      std::coroutine_handle<TaskPromise<void, Context>>::from_promise(*this)};
}

template <class Context> void TaskPromise<void, Context>::unhandled_exception() {
  mResult.template emplace<1>(std::current_exception());
}

template <class Context> void TaskPromise<void, Context>::return_void() noexcept {
  mResult.template emplace<2>(Unit{});
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                                       DefaultContext<Tp>

template <class Tp>
template <class Promise>
DefaultContext<Tp>::DefaultContext(std::coroutine_handle<Promise> handle) noexcept
    : mStopCallback(get_stop_token(get_env(handle.promise())), OnStopRequested{*this}) {}

template <class Tp> auto DefaultContext<Tp>::get_env() const noexcept -> Env { return Env{this}; }

///////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                           BasicTask<Tp, Context>::Awaiter

template <class Tp, class Context>
BasicTask<Tp, Context>::Awaiter::Awaiter(std::coroutine_handle<promise_type> handle) noexcept
    : mHandle(handle) {}

template <class Tp, class Context> BasicTask<Tp, Context>::Awaiter::~Awaiter() {
  if (mHandle) {
    mHandle.destroy();
  }
}

template <class Tp, class Context>
constexpr auto BasicTask<Tp, Context>::Awaiter::await_ready() noexcept {
  return false;
}

/// Set up continuation chain and start task execution via symmetric transfer
template <class Tp, class Context>
template <class OtherPromise>
auto BasicTask<Tp, Context>::Awaiter::await_suspend(
    std::coroutine_handle<OtherPromise> awaitingHandle) noexcept -> std::coroutine_handle<> {
  mHandle.promise().set_continuation(awaitingHandle);
  return mHandle;
}

/// Extract and return the task result, rethrowing any captured exception
template <class Tp, class Context> auto BasicTask<Tp, Context>::Awaiter::await_resume() -> Tp {
  auto& result = mHandle.promise().mResult;

  if (result.index() == 1) {
    std::rethrow_exception(std::get<1>(result));
  }

  if constexpr (!std::is_void_v<Tp>) {
    return std::get<2>(std::move(result));
  }
}

} // namespace ms