// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "IoTask.hpp"
#include "read_env.hpp"
#include "sync_wait.hpp"
#include "write_env.hpp"

#include <cassert>
#include <chrono>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

// Coroutines used in tests
auto coro_task_int() -> cw::Task<int> { co_return 42; }

auto coro_task_void() -> cw::Task<void> { co_return; }

auto coro_io_task_int() -> cw::IoTask<int> { co_return 42; }

auto coro_read_env_task() -> cw::IoTask<int> {
  cw::IoScheduler scheduler = co_await cw::read_env(cw::get_scheduler);
  co_await scheduler.schedule();
  co_return 42;
}

auto coro_schedule_delayed() -> cw::IoTask<void> {
  cw::IoScheduler scheduler = co_await cw::read_env(cw::get_scheduler);
  auto t0 = std::chrono::steady_clock::now();
  co_await scheduler.schedule_after(std::chrono::milliseconds(100));
  auto t1 = std::chrono::steady_clock::now();
  assert(t1 - t0 >= std::chrono::milliseconds(100));
}

auto coro_schedule_immediate() -> cw::IoTask<int> {
  cw::IoScheduler scheduler = co_await cw::read_env(cw::get_scheduler);
  co_await scheduler.schedule();
  co_return 123;
}

auto coro_schedule_at_absolute_time() -> cw::IoTask<void> {
  cw::IoScheduler scheduler = co_await cw::read_env(cw::get_scheduler);
  auto target = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
  co_await scheduler.schedule_at(target);
  auto actual = std::chrono::steady_clock::now();
  assert(actual >= target);
}

auto coro_multiple_timers() -> cw::IoTask<std::vector<int>> {
  cw::IoScheduler scheduler = co_await cw::read_env(cw::get_scheduler);
  std::vector<int> results;

  auto t0 = std::chrono::steady_clock::now();
  co_await scheduler.schedule_after(std::chrono::milliseconds(50));
  results.push_back(1);

  co_await scheduler.schedule_after(std::chrono::milliseconds(30));
  results.push_back(2);

  co_await scheduler.schedule_after(std::chrono::milliseconds(20));
  results.push_back(3);

  auto t1 = std::chrono::steady_clock::now();
  assert(t1 - t0 >= std::chrono::milliseconds(100));

  co_return results;
}

auto coro_poll_pipe_read() -> cw::IoTask<void> {
  cw::IoScheduler scheduler = co_await cw::read_env(cw::get_scheduler);

  int pipefd[2];
  assert(pipe(pipefd) == 0);

  // Make read end non-blocking
  int flags = fcntl(pipefd[0], F_GETFL, 0);
  fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

  // Write data to pipe
  const char data[] = "test";
  write(pipefd[1], data, sizeof(data));

  // Poll for read availability
  short events = co_await scheduler.poll(pipefd[0], POLLIN);
  assert(events & POLLIN);

  // Read the data
  char buffer[10];
  ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
  assert(n == sizeof(data));

  close(pipefd[0]);
  close(pipefd[1]);
}

auto coro_cancel_delayed_operation() -> cw::IoTask<bool> {
  cw::IoScheduler scheduler = co_await cw::read_env(cw::get_scheduler);
  std::stop_source stopSource;
  stopSource.request_stop();

  // This should be cancelled and not complete
  try {
    auto schedule = scheduler.schedule_after(std::chrono::seconds(10));
    auto stoppedSchedule =
        cw::write_env(std::move(schedule), cw::get_stop_token, stopSource.get_token());
    co_await std::move(stoppedSchedule);
    co_return false; // Should not reach here
  } catch (...) {
    co_return false; // Should not throw
  }
}

// Test cases
void test_sync_wait_with_int_result() {
  auto result = cw::sync_wait(coro_task_int());
  assert(result.has_value());
  assert(result.value() == 42);
}

void test_sync_wait_with_void_result() {
  bool result = cw::sync_wait(coro_task_void());
  assert(result);
}

void test_sync_wait_with_io_task() {
  auto result = cw::sync_wait(coro_io_task_int());
  assert(result.has_value());
  assert(result.value() == 42);
}

void test_sync_wait_with_read_env() {
  auto result = cw::sync_wait(coro_read_env_task());
  assert(result.has_value());
  assert(result.value() == 42);
}

void test_sync_wait_with_delayed_schedule() {
  bool result = cw::sync_wait(coro_schedule_delayed());
  assert(result);
}

void test_immediate_schedule() {
  auto result = cw::sync_wait(coro_schedule_immediate());
  assert(result.has_value());
  assert(result.value() == 123);
}

void test_absolute_time_schedule() {
  bool result = cw::sync_wait(coro_schedule_at_absolute_time());
  assert(result);
}

void test_multiple_sequential_timers() {
  auto result = cw::sync_wait(coro_multiple_timers());
  assert(result.has_value());
  assert(result.value().size() == 3);
  assert(result.value()[0] == 1);
  assert(result.value()[1] == 2);
  assert(result.value()[2] == 3);
}

void test_poll_operation() {
  bool result = cw::sync_wait(coro_poll_pipe_read());
  assert(result);
}

void test_cancel_delayed_operation() {
  auto result = cw::sync_wait(coro_cancel_delayed_operation());
  assert(!result.has_value());
}

int main() {
  // Basic sync_wait tests
  test_sync_wait_with_int_result();
  test_sync_wait_with_void_result();
  test_sync_wait_with_io_task();
  test_sync_wait_with_read_env();

  // IoContext scheduling tests
  test_immediate_schedule();
  test_sync_wait_with_delayed_schedule();
  test_absolute_time_schedule();
  test_multiple_sequential_timers();

  // I/O polling tests
  test_poll_operation();

  // Cancellation tests
  test_cancel_delayed_operation();
}