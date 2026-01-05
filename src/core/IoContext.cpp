// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "IoContext.hpp"

#include <algorithm>
#include <cassert>
#include <vector>

#include <sys/eventfd.h>
#include <sys/poll.h>
#include <system_error>
#include <unistd.h>

namespace ms {
IoContext::IoContext() {
  mWakeupHandle = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (mWakeupHandle == -1) {
    throw std::system_error(errno, std::generic_category(), "Failed to create eventfd");
  }
}

IoContext::~IoContext() { ::close(mWakeupHandle); }

void IoContext::enqueue(IoContextTaskCommand command) {
  {
    std::lock_guard lock(mTasksMutex);
    mTasks.push_back(command);
  }
  uint64_t value = 1;
  while (::write(mWakeupHandle, &value, sizeof(value)) == -1) {
    if (errno != EINTR) {
      throw std::system_error(errno, std::generic_category(), "Failed to write to eventfd");
    }
  }
}

// Min-heap priority queue for scheduled timers, ordered by expiration time
class TimerQueue {
public:
  void add_timer(IoContextTask* task) {
    mTasks.push_back(task);
    std::push_heap(mTasks.begin(), mTasks.end(), [](IoContextTask* a, IoContextTask* b) {
      return a->scheduledTime > b->scheduledTime;
    });
  }

  IoContextTask* remove_timer(IoContextTask* task) {
    auto it = std::find(mTasks.begin(), mTasks.end(), task);
    if (it != mTasks.end()) {
      std::swap(*it, mTasks.back());
      mTasks.pop_back();
      std::make_heap(mTasks.begin(), mTasks.end(), [](IoContextTask* a, IoContextTask* b) {
        return a->scheduledTime > b->scheduledTime;
      });
      return task;
    }
    return nullptr;
  }

  auto pop_expired(std::chrono::steady_clock::time_point now) -> IoContextTask* {
    if (mTasks.empty()) {
      return nullptr;
    }
    IoContextTask* task = mTasks.front();
    if (task->scheduledTime <= now) {
      std::pop_heap(mTasks.begin(), mTasks.end(), [](IoContextTask* a, IoContextTask* b) {
        return a->scheduledTime > b->scheduledTime;
      });
      mTasks.pop_back();
      return task;
    }
    return nullptr;
  }

  auto next_expiration() const -> std::optional<std::chrono::steady_clock::time_point> {
    if (mTasks.empty()) {
      return std::nullopt;
    }
    return mTasks.front()->scheduledTime;
  }

private:
  std::vector<IoContextTask*> mTasks;
};

void IoContext::run() noexcept try {
  // Event loop processes operations in priority order:
  // 1. Immediate tasks and new timers/polls from enqueued commands
  // 2. Expired timers from priority queue
  // 3. I/O events from ppoll (blocking with timeout until next timer)
  // Cancellation commands (StopTimed/StopPoll) remove pending operations.
  std::vector<IoContextTaskCommand> tasksToProcess;
  TimerQueue timerQueue;
  std::vector<IoContextTask*> ioPollTasks;
  std::vector<struct pollfd> pollFds;
  while (true) {
    {
      std::lock_guard lock(mTasksMutex);
      if (mStopRequested && mTasks.empty()) {
        break;
      }
      tasksToProcess.swap(mTasks);
    }

    for (IoContextTaskCommand command : tasksToProcess) {
      switch (command.kind) {
      case IoContextTaskCommand::Kind::Immediate:
        command.task->doCompletion(command.task);
        break;
      case IoContextTaskCommand::Kind::Timed:
        timerQueue.add_timer(command.task);
        break;
      case IoContextTaskCommand::Kind::StopTimed:
        // Remove timer from queue if still pending and complete it.
        // If timer already expired, remove_timer returns nullptr (no-op).
        if (IoContextTask* removed = timerQueue.remove_timer(command.task)) {
          removed->doCompletion(removed);
        }
        break;
      case IoContextTaskCommand::Kind::Poll:
        ioPollTasks.push_back(command.task);
        break;
      case IoContextTaskCommand::Kind::StopPoll: {
        // Remove poll operation from queue if still pending and complete it.
        // If poll already completed, find returns end() (no-op).
        auto operationIter = std::find(ioPollTasks.begin(), ioPollTasks.end(), command.task);
        if (operationIter != ioPollTasks.end()) {
          IoContextTask* removed = *operationIter;
          ioPollTasks.erase(operationIter);
          removed->doCompletion(removed);
        }
      } break;
      }
    }
    tasksToProcess.clear();

    auto now = std::chrono::steady_clock::now();
    while (IoContextTask* expiredTask = timerQueue.pop_expired(now)) {
      expiredTask->doCompletion(expiredTask);
      now = std::chrono::steady_clock::now(); // Refresh to account for completion time
    }

    pollFds.clear();
    pollFds.push_back({mWakeupHandle, POLLIN, 0});
    for (IoContextTask* task : ioPollTasks) {
      pollFds.push_back({task->pollFd, task->pollEvents, 0});
    }

    struct timespec timeoutSpec;
    struct timespec* timeoutPtr = nullptr;
    auto nextExpiration = timerQueue.next_expiration();
    if (nextExpiration) {
      now = std::chrono::steady_clock::now(); // Fresh timestamp for accurate timeout
      auto duration = *nextExpiration - now;
      if (duration.count() < 0) {
        duration = std::chrono::steady_clock::duration::zero();
      }
      auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
      auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds);
      timeoutSpec.tv_sec = seconds.count();
      timeoutSpec.tv_nsec = nanoseconds.count();
      timeoutPtr = &timeoutSpec;
    }

    if (::ppoll(pollFds.data(), pollFds.size(), timeoutPtr, nullptr) == -1) {
      if (errno == EINTR) {
        continue;
      } else {
        throw std::system_error(errno, std::generic_category(), "poll() failed");
      }
    }

    if (pollFds[0].revents & POLLIN) {
      uint64_t value;
      (void) ::read(mWakeupHandle, &value, sizeof(value));
    }

    // Process poll results: iterate both vectors in lockstep
    // pollFds[0] is the wakeup handle, so start from pollFds[1]
    auto operationIter = ioPollTasks.begin();
    auto pollfdIter = pollFds.begin() + 1;
    while (pollfdIter != pollFds.end() && operationIter != ioPollTasks.end()) {
      if (pollfdIter->revents != 0) {
        IoContextTask* task = *operationIter;
        task->pollEvents = pollfdIter->revents;
        task->doCompletion(task);
        operationIter = ioPollTasks.erase(operationIter);
      } else {
        ++operationIter;
      }
      ++pollfdIter;
    }
  }
} catch (...) {
  // Explicit terminate for static analysis tools that warn about noexcept violations
  std::terminate();
}

void IoContext::request_stop() {
  {
    std::lock_guard lock(mTasksMutex);
    mStopRequested = true;
  }
  uint64_t value = 1;
  while (::write(mWakeupHandle, &value, sizeof(value)) == -1) {
    if (errno != EINTR) {
      throw std::system_error(errno, std::generic_category(), "Failed to write to eventfd");
    }
  }
}

auto IoContext::get_scheduler() noexcept -> IoScheduler { return IoScheduler{*this}; }

IoScheduler::IoScheduler(IoContext& context) noexcept : mContext(&context) {}

auto IoScheduler::schedule() const noexcept -> ImmediateSender {
  return ImmediateSender{*mContext};
}

auto IoScheduler::schedule_after(std::chrono::steady_clock::duration delay) const noexcept
    -> DelaySender {
  return DelaySender{*mContext, delay};
}

auto IoScheduler::schedule_at(std::chrono::steady_clock::time_point timePoint) const noexcept
    -> TimedSender {
  return TimedSender{*mContext, timePoint};
}

auto IoScheduler::poll(int fd, short events) const noexcept -> PollSender {
  return PollSender{*mContext, fd, events};
}

ImmediateOperation::ImmediateOperation(IoContext& context) noexcept : mContext(context) {}

auto ImmediateOperation::await_ready() noexcept -> std::false_type { return {}; }

void ImmediateOperation::await_suspend(std::coroutine_handle<> handle) {
  this->mHandle = handle;
  this->doCompletion = +[](IoContextTask* task) noexcept {
    auto* op = static_cast<ImmediateOperation*>(task);
    op->mHandle.resume();
  };
  mContext.enqueue({this, IoContextTaskCommand::Kind::Immediate});
}

void ImmediateOperation::await_resume() noexcept {
  // No-op
}

ImmediateSender::ImmediateSender(IoContext& context) noexcept : mContext(&context) {}

auto ImmediateSender::operator co_await() const noexcept -> ImmediateOperation {
  return ImmediateOperation{*mContext};
}

DelayOperation::DelayOperation(IoContext& context,
                               std::chrono::steady_clock::duration delay) noexcept
    : CancellableOperation(context), mDelay(delay) {}

void DelayOperation::setup_operation() noexcept {
  this->scheduledTime = std::chrono::steady_clock::now() + mDelay;
}

void DelayOperation::await_resume() noexcept {
  // No-op
}

DelaySender::DelaySender(IoContext& context, std::chrono::steady_clock::duration delay) noexcept
    : mContext(&context), mDelay(delay) {}

auto DelaySender::operator co_await() const noexcept -> DelayOperation {
  return DelayOperation{*mContext, mDelay};
}

TimedOperation::TimedOperation(IoContext& context,
                               std::chrono::steady_clock::time_point timePoint) noexcept
    : CancellableOperation(context) {
  this->scheduledTime = timePoint;
}

void TimedOperation::setup_operation() noexcept {
  // scheduledTime already set in constructor
}

void TimedOperation::await_resume() noexcept {
  // No-op
}

TimedSender::TimedSender(IoContext& context,
                         std::chrono::steady_clock::time_point timePoint) noexcept
    : mContext(&context), mTimePoint(timePoint) {}

auto TimedSender::operator co_await() const noexcept -> TimedOperation {
  return TimedOperation{*mContext, mTimePoint};
}

PollOperation::PollOperation(IoContext& context, int fd, short events) noexcept
    : CancellableOperation(context) {
  this->pollFd = fd;
  this->pollEvents = events;
}

void PollOperation::setup_operation() noexcept {
  // pollFd and pollEvents already set in constructor
}

auto PollOperation::await_resume() noexcept -> short { return this->pollEvents; }

PollSender::PollSender(IoContext& context, int fd, short events) noexcept
    : mContext(&context), mFd(fd), mEvents(events) {}

auto PollSender::operator co_await() const noexcept -> PollOperation {
  return PollOperation{*mContext, mFd, mEvents};
}

} // namespace ms