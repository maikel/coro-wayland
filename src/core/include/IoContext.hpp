// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <chrono>
#include <exception>
#include <mutex>
#include <vector>

namespace ms {

struct IoContextTask {
  enum class Kind { Immediate, Delayed, Poll };

  void (*doCompletion)() noexcept;
  Kind kind;
  std::chrono::steady_clock::time_point scheduledTime;
  std::chrono::steady_clock::duration delay;
  int pollFd;
  short pollEvents;
};

class IoContext {
public:
  void schedule(IoContextTask& task);

  void run();
  void request_stop();

private:
  std::mutex mTasksMutex;
  std::vector<IoContextTask*> mTasks;
  bool mStopRequested = false;
  int mWakeupHandle;
};

} // namespace ms