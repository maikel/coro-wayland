// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Task.hpp"
#include "sync_wait.hpp"
#include "when_all.hpp"

#include <cassert>

auto coro_just(int value) -> ms::Task<int> { co_return value; }

void test_two_synchronous_awaitables() {
  auto whenAllTask = ms::when_all(coro_just(42), coro_just(43));
  auto result = ms::sync_wait(std::move(whenAllTask));
  assert(result.has_value());
  auto [result1, result2] = result.value();
  assert(result1 == 42);
  assert(result2 == 43);
}

int main() { test_two_synchronous_awaitables(); }