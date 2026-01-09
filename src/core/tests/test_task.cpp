// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Task.hpp"

#include <cassert>

// Coroutines used in tests
auto coro_task_void() -> cw::Task<void> { co_return; }

auto coro_task_int() -> cw::Task<int> { co_return 42; }

auto coro_task_in_task() -> cw::Task<int> {
  co_await coro_task_void();
  co_return 42;
}

// Driver coroutine type for testing Task awaiting
struct DriverCoro {
  struct promise_type {
    DriverCoro get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };
};

DriverCoro coro_await_task(int* value) {
  int retVal = co_await coro_task_int();
  *value = retVal;
}

DriverCoro coro_await_task_void(int* called) {
  co_await coro_task_void();
  *called = 1;
}

DriverCoro coro_await_task_in_task(int* value) {
  int retVal = co_await coro_task_in_task();
  *value = retVal;
}

// Test cases
void test_await_task_with_int_result() {
  int value = 0;
  coro_await_task(&value);
  assert(value == 42);
}

void test_await_task_with_void_result() {
  int called = 0;
  coro_await_task_void(&called);
  assert(called == 1);
}

void test_await_task_composition() {
  int value2 = 0;
  coro_await_task_in_task(&value2);
  assert(value2 == 42);
}

int main() {
  test_await_task_with_int_result();
  test_await_task_with_void_result();
  test_await_task_composition();
}