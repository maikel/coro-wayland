// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "IoContext.hpp"
#include "Task.hpp"

auto schedule_once(ms::IoContext& context) -> ms::Task<void> {
  co_await context.get_scheduler().schedule();
  context.request_stop();
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

auto test_await_schedule_once(ms::IoContext& context) -> Coro { co_await schedule_once(context); }

int main() {
  ms::IoContext ioContext;

  test_await_schedule_once(ioContext);

  ioContext.run();

  return 0;
}