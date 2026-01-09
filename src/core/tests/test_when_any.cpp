// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "IoTask.hpp"
#include "Task.hpp"
#include "just_stopped.hpp"
#include "read_env.hpp"
#include "sync_wait.hpp"
#include "when_any.hpp"

#include <cassert>

auto coro_void() -> cw::Task<void> { co_return; }

template <class Tp> auto coro_just(Tp value) -> cw::Task<Tp> { co_return value; }

auto coro_delayed(std::vector<int>& results, int value, std::chrono::milliseconds delay)
    -> cw::IoTask<void> {
  cw::IoScheduler scheduler = co_await cw::read_env(cw::get_scheduler);
  co_await scheduler.schedule_after(delay);
  results.push_back(value);
}

auto test_when_any_void() -> void {
  auto sender = cw::when_any(coro_void());
  std::optional<std::variant<std::monostate>> result = cw::sync_wait(std::move(sender));
  assert(result.has_value());
}

auto test_when_any_two_ints() -> void {
  auto sender = cw::when_any(coro_just(42), coro_just(7));
  auto result = cw::sync_wait(std::move(sender));
  assert(result.has_value());
  assert(result->index() == 0);
  assert(std::get<0>(*result) == 42);
}

auto test_when_any_one_slow_one_fast() -> void {
  std::vector<int> results;
  auto sender = cw::when_any(coro_delayed(results, 1, std::chrono::years(1)),
                             coro_delayed(results, 2, std::chrono::milliseconds(10)));
  auto t0 = std::chrono::steady_clock::now();
  auto result = cw::sync_wait(std::move(sender));
  auto t1 = std::chrono::steady_clock::now();
  assert(t1 - t0 >= std::chrono::milliseconds(10));
  assert(result.has_value());
  assert(result->index() == 1);
  assert(results.size() == 1);
  assert(results[0] == 2);
}

int main() {
  test_when_any_void();
  test_when_any_two_ints();
  test_when_any_one_slow_one_fast();
}