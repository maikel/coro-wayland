// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "IoTask.hpp"
#include "Task.hpp"
#include "just_stopped.hpp"
#include "read_env.hpp"
#include "sync_wait.hpp"
#include "when_all.hpp"

#include <cassert>

auto coro_just(int value) -> ms::Task<int> { co_return value; }

auto coro_delayed(std::vector<int>& results, int value, std::chrono::milliseconds delay)
    -> ms::IoTask<void> {
  ms::IoScheduler scheduler = co_await ms::read_env(ms::get_scheduler);
  co_await scheduler.schedule_after(delay);
  results.push_back(value);
}

auto coro_exception() -> ms::Task<int> {
  throw std::runtime_error("Test exception");
  co_return 0;
}

void test_two_synchronous_awaitables() {
  auto whenAllTask = ms::when_all(coro_just(42), coro_just(43));
  auto result = ms::sync_wait(std::move(whenAllTask));
  assert(result.has_value());
  auto [result1, result2] = result.value();
  assert(result1 == 42);
  assert(result2 == 43);
}

void test_multiple_delays() {
  std::vector<int> results;
  auto whenAllTask = ms::when_all(coro_delayed(results, 1, std::chrono::milliseconds(100)),
                                  coro_delayed(results, 2, std::chrono::milliseconds(50)),
                                  coro_delayed(results, 3, std::chrono::milliseconds(150)));
  auto t0 = std::chrono::steady_clock::now();
  auto result = ms::sync_wait(std::move(whenAllTask));
  auto t1 = std::chrono::steady_clock::now();
  assert(t1 - t0 >= std::chrono::milliseconds(150));
  assert(result.has_value());
  assert((results == std::vector<int>{2, 1, 3}));
}

void test_stopped_first_delay() {
  std::vector<int> results;
  auto whenAllTask = ms::when_all(ms::just_stopped(),                                       //
                                  coro_delayed(results, 1, std::chrono::milliseconds(100)), //
                                  coro_delayed(results, 2, std::chrono::milliseconds(50)));
  auto result = ms::sync_wait(std::move(whenAllTask));
  assert(!result.has_value());
  assert((results == std::vector<int>{}));
}

void test_stopped_last_delay() {
  std::vector<int> results;
  auto whenAllTask = ms::when_all(coro_delayed(results, 1, std::chrono::years(2)),
                                  coro_delayed(results, 2, std::chrono::years(3)),
                                  ms::just_stopped());
  auto result = ms::sync_wait(std::move(whenAllTask));
  assert(!result.has_value());
  assert((results == std::vector<int>{}));
}

void test_exception_first_delay() {
  std::vector<int> results;
  auto whenAllTask = ms::when_all(coro_exception(),
                                  coro_delayed(results, 1, std::chrono::milliseconds(100)),
                                  coro_delayed(results, 2, std::chrono::milliseconds(50)));
  try {
    ms::sync_wait(std::move(whenAllTask));
    assert(false); // Should not reach here
  } catch (const std::runtime_error& e) {
    assert(std::string(e.what()) == "Test exception");
  }
  assert((results == std::vector<int>{}));
}

void test_exception_last_delay() {
  std::vector<int> results;
  auto whenAllTask = ms::when_all(coro_delayed(results, 1, std::chrono::milliseconds(100)),
                                  coro_delayed(results, 2, std::chrono::milliseconds(50)),
                                  coro_exception());
  try {
    ms::sync_wait(std::move(whenAllTask));
    assert(false); // Should not reach here
  } catch (const std::runtime_error& e) {
    assert(std::string(e.what()) == "Test exception");
  }
  assert((results == std::vector<int>{}));
}

int main() {
  test_two_synchronous_awaitables();
  test_multiple_delays();
  test_stopped_first_delay();
  test_stopped_last_delay();
  test_exception_first_delay();
  test_exception_last_delay();
}