// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "Concepts.hpp"
#include "ManualLifetime.hpp"
#include "Queries.hpp"

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

/// Storage for task results: value or exception.
/// Provides uniform interface for setting and retrieving task outcomes.
template <class Tp> class TaskResult {
public:
  void set_error(std::exception_ptr error) noexcept;
  void set_value(Tp&& value) noexcept;

  auto get_result() -> Tp;

private:
  std::variant<std::monostate, std::exception_ptr, Tp> mResult;
};

template <> class TaskResult<void> {
public:
  void set_error(std::exception_ptr error) noexcept;
  void set_value() noexcept;

  auto get_result() -> void;

private:
  std::variant<std::monostate, std::exception_ptr, std::monostate> mResult;
};

/// Abstract interface for task operation state.
/// Provides the bridge between task coroutines and their execution context.
/// Handles continuation management, environment queries, and cancellation.
template <class Tp, class Env> class TaskOperationState : public TaskResult<Tp> {
public:
  virtual void set_stopped() noexcept = 0;
  virtual auto get_env() const noexcept -> Env = 0;
  virtual auto get_continuation() -> std::coroutine_handle<> = 0;

protected:
  virtual ~TaskOperationState() = default;
};

template <class Tp, class Traits, class... AwaitingPromise> class TaskAwaiter;
template <class Tp, class Traits> class TaskPromise;

/// A lazily-evaluated coroutine task that produces a value of type Tp.
///
/// @tparam Tp The result type of the task (use void for no result)
/// @tparam Traits Defines context and environment types for execution control
///
/// Tasks don't execute until co_await'ed, enabling composable async operations.
/// The Traits parameter allows customization of cancellation and environment behavior.
template <class Tp, class Traits> class BasicTask {
public:
  using promise_type = TaskPromise<Tp, Traits>;

  BasicTask(BasicTask&& other) noexcept;

  explicit BasicTask(std::coroutine_handle<TaskPromise<Tp, Traits>> handle) noexcept;

  ~BasicTask();

  template <class AwaitingPromise>
  auto connect(AwaitingPromise& promise) && noexcept -> TaskAwaiter<Tp, Traits, AwaitingPromise>;

  auto operator co_await() && -> TaskAwaiter<Tp, Traits>;

private:
  std::coroutine_handle<TaskPromise<Tp, Traits>> mHandle;
};

/// TaskAwaiter specialization for statically-known parent promise type.
/// Avoids virtual dispatch by storing concrete TaskContext<AwaitingPromise>.
/// Used when the awaiting coroutine's promise type is known at compile time.
template <class Tp, class Traits, class AwaitingPromise>
class TaskAwaiter<Tp, Traits, AwaitingPromise> : TaskOperationState<Tp, typename Traits::env_type> {
public:
  TaskAwaiter() = delete;
  TaskAwaiter(const TaskAwaiter&) = delete;
  TaskAwaiter(TaskAwaiter&&) = delete;
  TaskAwaiter& operator=(const TaskAwaiter&) = delete;
  TaskAwaiter& operator=(TaskAwaiter&&) = delete;

  explicit TaskAwaiter(std::coroutine_handle<TaskPromise<Tp, Traits>> handle,
                       AwaitingPromise& awaitingPromise) noexcept;

  ~TaskAwaiter();

  static constexpr auto await_ready() noexcept -> std::false_type;

  auto await_suspend(std::coroutine_handle<AwaitingPromise> parent) noexcept
      -> std::coroutine_handle<TaskPromise<Tp, Traits>>;

  auto await_resume() -> Tp;

private:
  void set_stopped() noexcept override;
  auto get_env() const noexcept -> typename Traits::env_type override;
  auto get_continuation() -> std::coroutine_handle<> override;

  std::coroutine_handle<TaskPromise<Tp, Traits>> mHandle;
  typename Traits::template context_type<AwaitingPromise> mContext;
};

/// TaskAwaiter specialization for type-erased parent promise.
/// Uses vtable-based dispatch via TaskContext<> for runtime polymorphism.
/// Used when awaited directly without static parent information.
template <class Tp, class Traits>
class TaskAwaiter<Tp, Traits> : TaskOperationState<Tp, typename Traits::env_type> {
public:
  TaskAwaiter() = delete;
  TaskAwaiter(const TaskAwaiter&) = delete;
  TaskAwaiter(TaskAwaiter&&) = delete;
  TaskAwaiter& operator=(const TaskAwaiter&) = delete;
  TaskAwaiter& operator=(TaskAwaiter&&) = delete;

  explicit TaskAwaiter(std::coroutine_handle<TaskPromise<Tp, Traits>> handle) noexcept;

  ~TaskAwaiter();

  static constexpr auto await_ready() noexcept -> std::false_type;

  template <class AwaitingPromise>
  auto await_suspend(std::coroutine_handle<AwaitingPromise> parent) noexcept
      -> std::coroutine_handle<TaskPromise<Tp, Traits>>;

  auto await_resume() -> Tp;

private:
  void set_stopped() noexcept override;
  auto get_env() const noexcept -> typename Traits::env_type override;
  auto get_continuation() -> std::coroutine_handle<> override;

  std::coroutine_handle<TaskPromise<Tp, Traits>> mHandle;
  ManualLifetime<typename Traits::template context_type<>> mContext;
};

/// Base promise type for task coroutines.
/// Manages coroutine lifecycle, continuation chains, and environment queries.
/// Stores pointer to operation state to delegate result/cancellation handling.
template <class Tp, class Traits> class TaskPromiseBase {
public:
  TaskPromiseBase() = default;

  /// Suspend at the start so task only executes when co_await'ed
  static constexpr auto initial_suspend() noexcept -> std::suspend_always;

  /// Final awaiter that implements symmetric transfer for continuation chaining.
  /// When the task completes, this resumes the awaiting coroutine without stack growth.
  struct FinalAwaiter {
    static constexpr auto await_ready() noexcept -> std::false_type;

    template <class OtherPromise>
    auto await_suspend(std::coroutine_handle<OtherPromise> handle) -> std::coroutine_handle<>;

    void await_resume() noexcept;
  };
  static constexpr auto final_suspend() noexcept -> FinalAwaiter;

  void set_operation_state(TaskOperationState<Tp, typename Traits::env_type>* opState) noexcept;

  void unhandled_exception();

  void unhandled_stopped();

  auto get_env() const noexcept -> typename Traits::env_type;

  /// Transform awaitable expressions to connect them with this promise.
  /// Enables sender/receiver integration and custom awaitable protocols.
  template <class Self, class Expression> auto await_transform(this Self& self, Expression&& expr);

protected:
  TaskOperationState<Tp, typename Traits::env_type>* mOpState = nullptr;
};

/// Promise type for tasks returning a value of type Tp
template <class Tp, class Traits> class TaskPromise : public TaskPromiseBase<Tp, Traits> {
public:
  TaskPromise() = default;

  auto get_return_object() noexcept -> BasicTask<Tp, Traits>;

  void return_value(Tp&& value) noexcept;
};

/// Promise type specialization for tasks returning void
template <class Traits> class TaskPromise<void, Traits> : public TaskPromiseBase<void, Traits> {
public:
  TaskPromise() = default;

  auto get_return_object() noexcept -> BasicTask<void, Traits>;

  void return_void() noexcept;
};

/// Environment for task execution providing stop token queries.
/// Encapsulates cancellation state propagated from parent coroutines.
class TaskEnv {
private:
  std::stop_token mStopToken;

public:
  explicit TaskEnv(std::stop_token stopToken) noexcept;

  auto query(get_stop_token_t) const noexcept -> std::stop_token;
};

template <class... AwaitingPromise> class TaskContext;

/// Task context with statically-known parent promise type.
/// Directly stores parent handle, avoiding indirection.
template <class AwaitingPromise> class TaskContext<AwaitingPromise> {
public:
  explicit TaskContext(std::coroutine_handle<AwaitingPromise> awaitingHandle) noexcept;

  auto get_continuation() const noexcept -> std::coroutine_handle<AwaitingPromise>;

  auto get_env() const noexcept -> TaskEnv;

  void set_stopped() noexcept;

private:
  std::coroutine_handle<AwaitingPromise> mAwaitingHandle;
};

/// Virtual function table for type-erased task contexts.
/// Enables runtime polymorphism for parent promise operations.
struct TaskContextVtable {
  auto (*get_continuation)(void*) noexcept -> std::coroutine_handle<>;
  auto (*get_env)(const void*) noexcept -> TaskEnv;
  void (*set_stopped)(void*) noexcept;
};

/// Compile-time vtable instance for specific promise type.
/// Static functions cast void* back to concrete promise type.
template <class AwaitingPromise>
inline constexpr TaskContextVtable TaskContextVtableFor = {
    /*get_continuation*/ +[](void* pointer) noexcept -> std::coroutine_handle<> {
      auto* promise = static_cast<AwaitingPromise*>(pointer);
      return std::coroutine_handle<AwaitingPromise>::from_promise(*promise);
    },
    /*get_env*/
    +[](const void* pointer) noexcept -> TaskEnv {
      auto* promise = static_cast<const AwaitingPromise*>(pointer);
      return TaskEnv{::ms::get_stop_token(::ms::get_env(*promise))};
    },
    /*set_stopped*/
    +[](void* pointer) noexcept {
      auto* promise = static_cast<AwaitingPromise*>(pointer);
      if constexpr (requires { promise->unhandled_stopped(); }) {
        promise->unhandled_stopped();
      } else {
        try {
          throw std::system_error(std::make_error_code(std::errc::operation_canceled));
        } catch (...) {
          promise->unhandled_exception();
        }
      }
    }};

/// Type-erased task context using vtable dispatch.
/// Stores void* to parent promise with vtable for dynamic operations.
template <> class TaskContext<> {
public:
  template <class AwaitingPromise>
  explicit TaskContext(std::coroutine_handle<AwaitingPromise> awaitingHandle) noexcept;

  auto get_continuation() const noexcept -> std::coroutine_handle<>;

  auto get_env() const noexcept -> TaskEnv;

  void set_stopped() noexcept;

private:
  const TaskContextVtable* mVtable;
  void* mPromise;
};

/// Default traits for Task, defining context and environment types.
struct TaskTraits {
  template <class... AwaitingPromise> using context_type = TaskContext<AwaitingPromise...>;

  using env_type = TaskEnv;
};

/// Convenient alias for BasicTask with default cancellation support
template <class Tp> using Task = BasicTask<Tp, TaskTraits>;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                                  TaskResult<Tp>

template <class Tp> void TaskResult<Tp>::set_error(std::exception_ptr error) noexcept {
  mResult.template emplace<1>(error);
}

template <class Tp> void TaskResult<Tp>::set_value(Tp&& value) noexcept {
  mResult.template emplace<2>(std::forward<Tp>(value));
}

template <class Tp> auto TaskResult<Tp>::get_result() -> Tp {
  if (mResult.index() == 1) {
    std::rethrow_exception(std::get<1>(mResult));
  } else {
    return std::get<2>(mResult);
  }
}

void TaskResult<void>::set_error(std::exception_ptr error) noexcept {
  mResult.template emplace<1>(error);
}

void TaskResult<void>::set_value() noexcept { mResult.template emplace<2>(std::monostate{}); }

auto TaskResult<void>::get_result() -> void {
  if (mResult.index() == 1) {
    std::rethrow_exception(std::get<1>(mResult));
  }
}

template <class Tp, class Traits>
BasicTask<Tp, Traits>::BasicTask(BasicTask&& other) noexcept : mHandle(other.mHandle) {
  other.mHandle = nullptr;
}

template <class Tp, class Traits>
BasicTask<Tp, Traits>::BasicTask(std::coroutine_handle<TaskPromise<Tp, Traits>> handle) noexcept
    : mHandle(handle) {}

template <class Tp, class Traits> BasicTask<Tp, Traits>::~BasicTask() {
  if (mHandle) {
    mHandle.destroy();
  }
}

template <class Tp, class Traits>
template <class AwaitingPromise>
auto BasicTask<Tp, Traits>::connect(AwaitingPromise& promise) && noexcept
    -> TaskAwaiter<Tp, Traits, AwaitingPromise> {
  return TaskAwaiter<Tp, Traits, AwaitingPromise>{std::exchange(mHandle, nullptr), promise};
}

template <class Tp, class Traits>
auto BasicTask<Tp, Traits>::operator co_await() && -> TaskAwaiter<Tp, Traits> {
  return TaskAwaiter<Tp, Traits>{std::exchange(mHandle, nullptr)};
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details TaskContext<AwaitingPromise>

template <class AwaitingPromise>
TaskContext<AwaitingPromise>::TaskContext(
    std::coroutine_handle<AwaitingPromise> awaitingHandle) noexcept
    : mAwaitingHandle(awaitingHandle) {}

template <class AwaitingPromise>
auto TaskContext<AwaitingPromise>::get_continuation() const noexcept
    -> std::coroutine_handle<AwaitingPromise> {
  return mAwaitingHandle;
}

template <class AwaitingPromise>
auto TaskContext<AwaitingPromise>::get_env() const noexcept -> TaskEnv {
  return TaskEnv{::ms::get_stop_token(::ms::get_env(mAwaitingHandle.promise()))};
}

/// Propagate cancellation to parent coroutine.
/// Prefers unhandled_stopped() if available, otherwise converts to exception.
template <class AwaitingPromise> void TaskContext<AwaitingPromise>::set_stopped() noexcept {
  if constexpr (requires { mAwaitingHandle.promise().unhandled_stopped(); }) {
    mAwaitingHandle.promise().unhandled_stopped();
  } else {
    try {
      throw std::system_error(std::make_error_code(std::errc::operation_canceled));
    } catch (...) {
      mAwaitingHandle.promise().unhandled_exception();
    }
  }
}

template <class AwaitingPromise>
TaskContext<>::TaskContext(std::coroutine_handle<AwaitingPromise> awaitingHandle) noexcept
    : mVtable(&TaskContextVtableFor<AwaitingPromise>),
      mPromise(static_cast<void*>(&awaitingHandle.promise())) {}

inline auto TaskContext<>::get_env() const noexcept -> TaskEnv {
  return mVtable->get_env(mPromise);
}

inline void TaskContext<>::set_stopped() noexcept { mVtable->set_stopped(mPromise); }

inline auto TaskContext<>::get_continuation() const noexcept -> std::coroutine_handle<> {
  return mVtable->get_continuation(mPromise);
}

inline TaskEnv::TaskEnv(std::stop_token stopToken) noexcept : mStopToken(stopToken) {}

inline auto TaskEnv::query(get_stop_token_t) const noexcept -> std::stop_token {
  return mStopToken;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                TaskAwaiter<Tp, Traits, AwaitingPromise>

template <class Tp, class Traits, class AwaitingPromise>
TaskAwaiter<Tp, Traits, AwaitingPromise>::TaskAwaiter(
    std::coroutine_handle<TaskPromise<Tp, Traits>> handle,
    AwaitingPromise& awaitingPromise) noexcept
    : mHandle(handle),
      mContext(std::coroutine_handle<AwaitingPromise>::from_promise(awaitingPromise)) {}

template <class Tp, class Traits, class AwaitingPromise>
TaskAwaiter<Tp, Traits, AwaitingPromise>::~TaskAwaiter() {
  if (mHandle) {
    mHandle.destroy();
  }
}

template <class Tp, class Traits, class AwaitingPromise>
constexpr auto TaskAwaiter<Tp, Traits, AwaitingPromise>::await_ready() noexcept -> std::false_type {
  return {};
}

/// Suspend and transfer control to the task via symmetric transfer.
/// Links promise to this awaiter before starting task execution.
template <class Tp, class Traits, class AwaitingPromise>
auto TaskAwaiter<Tp, Traits, AwaitingPromise>::await_suspend(
    std::coroutine_handle<AwaitingPromise>) noexcept
    -> std::coroutine_handle<TaskPromise<Tp, Traits>> {
  mHandle.promise().set_operation_state(this);
  return mHandle;
}

template <class Tp, class Traits, class AwaitingPromise>
auto TaskAwaiter<Tp, Traits, AwaitingPromise>::await_resume() -> Tp {
  return this->get_result();
}

template <class Tp, class Traits, class AwaitingPromise>
void TaskAwaiter<Tp, Traits, AwaitingPromise>::set_stopped() noexcept {
  mContext.set_stopped();
}

template <class Tp, class Traits, class AwaitingPromise>
auto TaskAwaiter<Tp, Traits, AwaitingPromise>::get_env() const noexcept ->
    typename Traits::env_type {
  return mContext.get_env();
}

template <class Tp, class Traits, class AwaitingPromise>
auto TaskAwaiter<Tp, Traits, AwaitingPromise>::get_continuation() -> std::coroutine_handle<> {
  return mContext.get_continuation();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                                TaskAwaiter<Tp, Traits>

template <class Tp, class Traits>
TaskAwaiter<Tp, Traits>::TaskAwaiter(std::coroutine_handle<TaskPromise<Tp, Traits>> handle) noexcept
    : mHandle(handle) {}

template <class Tp, class Traits> TaskAwaiter<Tp, Traits>::~TaskAwaiter() {
  if (mHandle) {
    mHandle.destroy();
  }
}

template <class Tp, class Traits>
constexpr auto TaskAwaiter<Tp, Traits>::await_ready() noexcept -> std::false_type {
  return {};
}

template <class Tp, class Traits>
template <class AwaitingPromise>
auto TaskAwaiter<Tp, Traits>::await_suspend(
    std::coroutine_handle<AwaitingPromise> awaitingHandle) noexcept
    -> std::coroutine_handle<TaskPromise<Tp, Traits>> {
  mContext.emplace(awaitingHandle);
  mHandle.promise().set_operation_state(this);
  return mHandle;
}

template <class Tp, class Traits> auto TaskAwaiter<Tp, Traits>::await_resume() -> Tp {
  return this->get_result();
}

template <class Tp, class Traits> void TaskAwaiter<Tp, Traits>::set_stopped() noexcept {
  mContext->set_stopped();
}

template <class Tp, class Traits>
auto TaskAwaiter<Tp, Traits>::get_env() const noexcept -> typename Traits::env_type {
  return mContext->get_env();
}

template <class Tp, class Traits>
auto TaskAwaiter<Tp, Traits>::get_continuation() -> std::coroutine_handle<> {
  return mContext->get_continuation();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                             TaskPromiseBase<Tp, Context>

template <class Tp, class Traits>
constexpr auto TaskPromiseBase<Tp, Traits>::initial_suspend() noexcept -> std::suspend_always {
  return {};
}

template <class Tp, class Traits>
constexpr auto TaskPromiseBase<Tp, Traits>::final_suspend() noexcept -> FinalAwaiter {
  return {};
}

template <class Tp, class Traits> void TaskPromiseBase<Tp, Traits>::unhandled_stopped() {
  mOpState->set_stopped();
}

template <class Tp, class Traits> void TaskPromiseBase<Tp, Traits>::unhandled_exception() {
  mOpState->set_error(std::current_exception());
}

/// Connect promise to operation state (awaiter).
/// Called by awaiter on suspension to establish bidirectional link.
template <class Tp, class Traits>
void TaskPromiseBase<Tp, Traits>::set_operation_state(
    TaskOperationState<Tp, typename Traits::env_type>* opState) noexcept {
  mOpState = opState;
}

/// Query environment from operation state.
/// Delegates to awaiter which holds parent context.
template <class Tp, class Traits>
auto TaskPromiseBase<Tp, Traits>::get_env() const noexcept -> typename Traits::env_type {
  return mOpState->get_env();
}

template <class Tp, class Traits>
constexpr auto TaskPromiseBase<Tp, Traits>::FinalAwaiter::await_ready() noexcept
    -> std::false_type {
  return {};
}

template <class Tp, class Traits>
template <class Self, class Expression>
auto TaskPromiseBase<Tp, Traits>::await_transform(this Self& self, Expression&& expr) {
  if constexpr (requires { std::forward<Expression>(expr).connect(self); }) {
    return std::forward<Expression>(expr).connect(self);
  } else {
    return std::forward<Expression>(expr);
  }
}

/// Implement symmetric transfer: return continuation instead of resuming directly.
/// This prevents unbounded stack growth in long continuation chains.
template <class Tp, class Traits>
template <class OtherPromise>
auto TaskPromiseBase<Tp, Traits>::FinalAwaiter::await_suspend(
    std::coroutine_handle<OtherPromise> handle) -> std::coroutine_handle<> {
  return handle.promise().mOpState->get_continuation();
}

template <class Tp, class Traits>
void TaskPromiseBase<Tp, Traits>::FinalAwaiter::await_resume() noexcept {}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                                 TaskPromise<Tp, Traits>

template <class Tp, class Traits>
auto TaskPromise<Tp, Traits>::get_return_object() noexcept -> BasicTask<Tp, Traits> {
  return BasicTask<Tp, Traits>{std::coroutine_handle<TaskPromise<Tp, Traits>>::from_promise(*this)};
}

template <class Tp, class Traits> void TaskPromise<Tp, Traits>::return_value(Tp&& value) noexcept {
  this->mOpState->set_value(std::forward<Tp>(value));
}

template <class Traits>
auto TaskPromise<void, Traits>::get_return_object() noexcept -> BasicTask<void, Traits> {
  return BasicTask<void, Traits>{
      std::coroutine_handle<TaskPromise<void, Traits>>::from_promise(*this)};
}

template <class Traits> void TaskPromise<void, Traits>::return_void() noexcept {
  this->mOpState->set_value();
}

} // namespace ms