// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <chrono>
#include <exception>
#include <mutex>
#include <vector>

namespace ms {

struct IoContextTask {
  enum class Kind { Immediate, Delayed, Poll, StopDelayed, StopPoll };

  void (*doCompletion)(IoContextTask*) noexcept;
  Kind kind;
  std::chrono::steady_clock::time_point scheduledTime;
  std::chrono::steady_clock::duration delay;
  int pollFd;
  short pollEvents;
  IoContextTask* stopTarget;
};

class IoContext {
public:
  IoContext();
  IoContext(const IoContext&) = delete;
  IoContext& operator=(const IoContext&) = delete;
  IoContext(IoContext&&) = delete;
  IoContext& operator=(IoContext&&) = delete;
  ~IoContext();

  void enqueue(IoContextTask& task);

  void run() noexcept;
  void request_stop();

private:
  std::mutex mTasksMutex;
  std::vector<IoContextTask*> mTasks;
  bool mStopRequested = false;
  int mWakeupHandle;
};

// class IoScheduler {
// public:
//   explicit IoScheduler(IoContext& context) : mContext(context) {}

//   ImmediateAwaitable schedule(IoContextTask& task);
// };

} // namespace ms