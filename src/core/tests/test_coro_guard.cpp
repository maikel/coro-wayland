// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "coro_guard.hpp"
#include "just_stopped.hpp"
#include "sync_wait.hpp"

#include <cassert>

auto coro_push_back(std::vector<int>& vec, int value) -> cw::Task<void> {
  vec.push_back(value);
  co_return;
}

auto coro_guard_test(std::vector<int>& vec) -> cw::Task<void> {
  co_await cw::coro_guard(coro_push_back(vec, 1));
  co_await coro_push_back(vec, 2);
}

auto coro_guard_test_stopped(std::vector<int>& vec) -> cw::Task<void> {
  co_await cw::coro_guard(coro_push_back(vec, 1));
  co_await coro_push_back(vec, 2);
  co_await cw::just_stopped();
  co_await coro_push_back(vec, 3);
}

auto coro_guard_test_exception(std::vector<int>& vec) -> cw::Task<void> {
  co_await cw::coro_guard(coro_push_back(vec, 1));
  co_await coro_push_back(vec, 2);
  throw std::runtime_error("Test exception");
  co_await coro_push_back(vec, 3);
}

auto test_coro_guard() -> void {
  std::vector<int> vec;
  cw::sync_wait(coro_guard_test(vec));
  assert((vec == std::vector<int>{2, 1}));
}

auto test_coro_guard_stopped() -> void {
  std::vector<int> vec;
  bool success = cw::sync_wait(coro_guard_test_stopped(vec));
  assert(!success);
  assert((vec == std::vector<int>{2, 1}));
}

auto test_coro_guard_exception() -> void {
  std::vector<int> vec;
  bool exceptionThrown = false;
  try {
    cw::sync_wait(coro_guard_test_exception(vec));
  } catch (const std::runtime_error& e) {
    exceptionThrown = true;
    assert(std::string{e.what()} == "Test exception");
  }
  assert(exceptionThrown);
  assert((vec == std::vector<int>{2, 1}));
}

int main() {
  test_coro_guard();
  test_coro_guard_stopped();
  test_coro_guard_exception();
}