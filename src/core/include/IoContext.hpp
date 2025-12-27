// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "ImmovableBase.hpp"
#include "ManualLifetime.hpp"
#include "Queries.hpp"

#include <chrono>
#include <coroutine>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <vector>

namespace ms {

/// Task descriptor for IoContext operations.
/// Contains scheduling information and completion callback.
/// Fields serve dual purpose: input parameters and output results (e.g., pollEvents).
struct IoContextTask {
  void (*doCompletion)(IoContextTask*) noexcept;
  std::chrono::steady_clock::time_point scheduledTime;
  int pollFd;
  short pollEvents;
};

struct IoContextTaskCommand {
  enum class Kind { Immediate, Timed, Poll, StopTimed, StopPoll };
  IoContextTask* task;
  Kind kind;
};

class IoScheduler;

/// Single-threaded event loop for asynchronous I/O operations.
/// Manages immediate tasks, timers, and file descriptor polling.
/// Thread-safe enqueue, single-threaded execution via run().
class IoContext : ImmovableBase {
public:
  IoContext();
  ~IoContext();

  /// Enqueue a task for execution. Thread-safe.
  void enqueue(IoContextTaskCommand command);

  /// Run the event loop until request_stop() is called. Must be called from a single thread.
  void run() noexcept;

  /// Request the event loop to stop gracefully.
  void request_stop();

  /// Get a scheduler for this context.
  auto get_scheduler() noexcept -> IoScheduler;

private:
  std::mutex mTasksMutex;
  std::vector<IoContextTaskCommand> mTasks;
  bool mStopRequested = false;
  int mWakeupHandle;
};

/// CRTP base class for IoContext operations supporting stop_token cancellation.
/// Implements the reference counting pattern to handle races between operation
/// completion and stop requests.
///
/// Derived classes must provide:
/// - IoContextTaskCommand::Kind enqueue_kind() const noexcept
/// - IoContextTaskCommand::Kind stop_kind() const noexcept
/// - void setup_operation() noexcept  // Called before enqueuing, optional
template <class Derived> class CancellableOperation : public IoContextTask, public ImmovableBase {
public:
  static auto await_ready() noexcept -> std::false_type;

  template <class Promise> void await_suspend(std::coroutine_handle<Promise> handle) noexcept;

protected:
  void do_await_suspend(std::stop_token token) noexcept;

  static void completion_callback(IoContextTask* task) noexcept;

  struct OnStopRequested {
    void operator()() const noexcept;
    Derived& mOp;
  };

  explicit CancellableOperation(IoContext& context) noexcept;

  IoContext& mContext;
  ManualLifetime<std::stop_callback<OnStopRequested>> mStopCallback;
  std::mutex mMutex;         // Synchronizes mRefCount between operation/stop paths
  std::uint8_t mRefCount{0}; // Race detector: counts operation enqueue + stop request
  std::coroutine_handle<> mHandle;
  std::function<void()> mSetStopped; // Invokes promise.unhandled_stopped()
};

/// Awaitable operation that yields to the event loop immediately.
/// Resumes on the next iteration after all currently queued tasks.
class ImmediateOperation : IoContextTask, ImmovableBase {
public:
  explicit ImmediateOperation(IoContext& context) noexcept;

  static auto await_ready() noexcept -> std::false_type;
  void await_suspend(std::coroutine_handle<> handle);
  void await_resume() noexcept;

private:
  IoContext& mContext;
  std::coroutine_handle<> mHandle;
};

/// Sender for immediate scheduling operations.
class ImmediateSender {
public:
  explicit ImmediateSender(IoContext& context) noexcept;
  auto operator co_await() const noexcept -> ImmediateOperation;

private:
  IoContext* mContext;
};

/// Awaitable operation that completes after a relative time delay.
/// Timer starts when the operation is co_await'ed.
/// Supports cancellation via stop_token with reference counting to handle races.
class DelayOperation : public CancellableOperation<DelayOperation> {
public:
  explicit DelayOperation(IoContext& context, std::chrono::steady_clock::duration delay) noexcept;

  void await_resume() noexcept;

  auto enqueue_kind() const noexcept { return IoContextTaskCommand::Kind::Timed; }
  auto stop_kind() const noexcept { return IoContextTaskCommand::Kind::StopTimed; }

private:
  friend class CancellableOperation<DelayOperation>;
  void setup_operation() noexcept;

  std::chrono::steady_clock::duration mDelay;
};

/// Sender for delay-based scheduling operations.
class DelaySender {
public:
  explicit DelaySender(IoContext& context, std::chrono::steady_clock::duration delay) noexcept;
  auto operator co_await() const noexcept -> DelayOperation;

private:
  IoContext* mContext;
  std::chrono::steady_clock::duration mDelay;
};

/// Awaitable operation that completes at an absolute time point.
/// Timer is scheduled for the specified time regardless of when co_await is called.
/// Supports cancellation via stop_token with reference counting to handle races.
class TimedOperation : public CancellableOperation<TimedOperation> {
public:
  explicit TimedOperation(IoContext& context,
                          std::chrono::steady_clock::time_point timePoint) noexcept;

  void await_resume() noexcept;

  auto enqueue_kind() const noexcept { return IoContextTaskCommand::Kind::Timed; }
  auto stop_kind() const noexcept { return IoContextTaskCommand::Kind::StopTimed; }

private:
  friend class CancellableOperation<TimedOperation>;
  void setup_operation() noexcept;
};

/// Sender for absolute time-based scheduling operations.
class TimedSender {
public:
  explicit TimedSender(IoContext& context,
                       std::chrono::steady_clock::time_point timePoint) noexcept;
  auto operator co_await() const noexcept -> TimedOperation;

private:
  IoContext* mContext;
  std::chrono::steady_clock::time_point mTimePoint;
};

/// Awaitable operation for file descriptor polling (one-shot).
/// Completes when the specified events occur on the file descriptor.
/// Returns the actual events that occurred via await_resume().
/// Supports cancellation via stop_token with reference counting to handle races.
class PollOperation : public CancellableOperation<PollOperation> {
public:
  explicit PollOperation(IoContext& context, int fd, short events) noexcept;

  /// Returns the actual poll events that occurred (revents from poll).
  short await_resume() noexcept;

  auto enqueue_kind() const noexcept { return IoContextTaskCommand::Kind::Poll; }
  auto stop_kind() const noexcept { return IoContextTaskCommand::Kind::StopPoll; }

private:
  friend class CancellableOperation<PollOperation>;
  void setup_operation() noexcept;
};

/// Sender for file descriptor polling operations.
class PollSender {
public:
  explicit PollSender(IoContext& context, int fd, short events) noexcept;
  auto operator co_await() const noexcept -> PollOperation;

private:
  IoContext* mContext;
  int mFd;
  short mEvents;
};

/// Scheduler interface for IoContext operations.
/// Provides factory methods for creating schedulable async operations.
class IoScheduler {
public:
  explicit IoScheduler(IoContext& context) noexcept;

  /// Schedule immediate execution (next event loop iteration).
  auto schedule() const noexcept -> ImmediateSender;

  /// Schedule execution after a relative delay.
  auto schedule_after(std::chrono::steady_clock::duration delay) const noexcept -> DelaySender;

  /// Schedule execution at an absolute time point.
  auto schedule_at(std::chrono::steady_clock::time_point timePoint) const noexcept -> TimedSender;

  /// Poll a file descriptor for specified events (POLLIN, POLLOUT, etc.).
  auto poll(int fd, short events) const noexcept -> PollSender;

  friend auto operator==(const IoScheduler& lhs, const IoScheduler& rhs) noexcept -> bool = default;

private:
  IoContext* mContext;
};

template <class Derived>
auto CancellableOperation<Derived>::await_ready() noexcept -> std::false_type {
  return {};
}

template <class Derived>
template <class Promise>
void CancellableOperation<Derived>::await_suspend(std::coroutine_handle<Promise> handle) noexcept {
  mHandle = handle;
  auto* promise = &handle.promise();
  mSetStopped = [promise] { promise->unhandled_stopped(); };
  std::stop_token token = ms::get_stop_token(ms::get_env(handle.promise()));
  this->do_await_suspend(token);
}

template <class Derived>
void CancellableOperation<Derived>::do_await_suspend(std::stop_token token) noexcept {
  // Allow derived class to perform operation-specific setup (e.g., compute scheduledTime)
  static_cast<Derived*>(this)->setup_operation();
  this->doCompletion = &CancellableOperation::completion_callback;
  // Register stop callback before checking mRefCount to avoid races
  mStopCallback.emplace(token, OnStopRequested{static_cast<Derived&>(*this)});
  std::unique_lock lock{mMutex};
  std::uint8_t oldRefCount = mRefCount++;
  if (oldRefCount == 0) {
    // First to increment: enqueue operation normally.
    // Holding the mutex ensures stop commands are enqueued strictly AFTER this operation.
    mContext.enqueue({this, static_cast<Derived*>(this)->enqueue_kind()});
  } else {
    // oldRefCount == 1: Stop callback already incremented, immediately cancel
    lock.unlock();
    mSetStopped();
  }
}

template <class Derived>
void CancellableOperation<Derived>::completion_callback(IoContextTask* task) noexcept {
  auto* op = static_cast<CancellableOperation*>(task);
  // Destroy stop_callback first - its destructor blocks until concurrent
  // OnStopRequested::operator() completes, ensuring mRefCount is stable
  op->mStopCallback.destroy();
  {
    // Wait for the enqueuing thread (in do_await_suspend) to release the mutex.
    // Resuming the coroutine or invoking mSetStopped() may destroy this operation,
    // so we must ensure the mutex is not held by do_await_suspend before proceeding.
    // This prevents use-after-free if await_suspend is called from a different thread.
    std::unique_lock lock{op->mMutex};
  }
  if (op->mRefCount == 1) {
    // Operation completed before stop requested - resume normally
    op->mHandle.resume();
  } else {
    // mRefCount == 2: Stop was requested, invoke unhandled_stopped()
    op->mSetStopped();
  }
}

template <class Derived>
void CancellableOperation<Derived>::OnStopRequested::operator()() const noexcept {
  std::unique_lock lock{mOp.mMutex};
  std::uint8_t oldRefCount = mOp.mRefCount++;
  if (oldRefCount == 0) {
    // Stop won the race: operation not yet enqueued. do_await_suspend will see
    // mRefCount == 1 and immediately call mSetStopped() without enqueueing.
  } else if (oldRefCount == 1) {
    // Operation already enqueued: send stop command to remove it from queue.
    // doCompletion will see mRefCount == 2 and invoke mSetStopped().
    mOp.mContext.enqueue({&mOp, mOp.stop_kind()});
  }
  // Note: oldRefCount can never be > 1 because only two paths increment mRefCount
}

template <class Derived>
CancellableOperation<Derived>::CancellableOperation(IoContext& context) noexcept
    : mContext(context) {}

} // namespace ms