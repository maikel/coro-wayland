// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "IoTask.hpp"
#include "read_env.hpp"
#include "sync_wait.hpp"

#include <cassert>

// Coroutines used in tests
auto coro_task_int() -> ms::Task<int> { co_return 42; }

auto coro_task_void() -> ms::Task<void> { co_return; }

auto coro_io_task_int() -> ms::IoTask<int> { co_return 42; }

auto coro_read_env_task() -> ms::IoTask<int> {
  ms::IoScheduler scheduler = co_await ms::read_env(ms::get_scheduler);
  co_await scheduler.schedule();
  co_return 42;
}

auto coro_schedule_delayed() -> ms::IoTask<void> {
  ms::IoScheduler scheduler = co_await ms::read_env(ms::get_scheduler);
  auto t0 = std::chrono::steady_clock::now();
  co_await scheduler.schedule_after(std::chrono::milliseconds(100));
  auto t1 = std::chrono::steady_clock::now();
  assert(t1 - t0 >= std::chrono::milliseconds(100));
}

// Test cases
void test_sync_wait_with_int_result() {
  auto result = ms::sync_wait(coro_task_int());
  assert(result.has_value());
  assert(result.value() == 42);
}

void test_sync_wait_with_void_result() {
  bool result = ms::sync_wait(coro_task_void());
  assert(result);
}

void test_sync_wait_with_io_task() {
  auto result = ms::sync_wait(coro_io_task_int());
  assert(result.has_value());
  assert(result.value() == 42);
}

void test_sync_wait_with_read_env() {
  auto result = ms::sync_wait(coro_read_env_task());
  assert(result.has_value());
  assert(result.value() == 42);
}

void test_sync_wait_with_delayed_schedule() {
  bool result = ms::sync_wait(coro_schedule_delayed());
  assert(result);
}

int main() {
  test_sync_wait_with_int_result();
  test_sync_wait_with_void_result();
  test_sync_wait_with_io_task();
  test_sync_wait_with_read_env();
  test_sync_wait_with_delayed_schedule();
}