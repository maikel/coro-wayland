// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Task.hpp"

#include <cassert>

auto test_task() -> ms::Task<int> { co_return 42; }

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

int main() {
  int value = 0;
  test_await_task(&value);
  assert(value == 42);
}