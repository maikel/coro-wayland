// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Task.hpp"

#include <cassert>

auto test_task_void() -> ms::Task<void> { co_return; }

auto test_task() -> ms::Task<int> { co_return 42; }

auto test_task_in_task() -> ms::Task<int> {
  co_await test_task_void();
  co_return 42;
}

struct Coro {
  struct promise_type {
    Coro get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };
};

Coro test_await_task(int* value) {
  int retVal = co_await test_task();
  *value = retVal;
}

Coro test_await_task_void(int* called) {
  co_await test_task_void();
  *called = 1;
}

Coro test_await_task_in_task(int* value) {
  int retVal = co_await test_task_in_task();
  *value = retVal;
}

int main() {
  int value = 0;
  test_await_task(&value);
  assert(value == 42);

  int called = 0;
  test_await_task_void(&called);
  assert(called == 1);

  int value2 = 0;
  test_await_task_in_task(&value2);
  assert(value2 == 42);
}