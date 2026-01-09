// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "AsyncScope.hpp"
#include "just_stopped.hpp"
#include "sync_wait.hpp"

auto coro_void() -> cw::Task<void> { co_return; }

auto coro_exception() -> cw::Task<void> {
  throw std::runtime_error("Test exception");
  co_return;
}

auto coro_stopped() -> cw::Task<void> { co_await cw::just_stopped(); }

void test_async_scope_void() {
  cw::AsyncScope scope;
  scope.spawn(coro_void());
  cw::sync_wait(scope.close());
}

void test_async_scope_exception() {
  cw::AsyncScope scope;
  scope.spawn(coro_exception());
  cw::sync_wait(scope.close());
}

void test_async_scope_stopped() {
  cw::AsyncScope scope;
  scope.spawn(coro_stopped());
  cw::sync_wait(scope.close());
}

int main() {
  test_async_scope_void();
  test_async_scope_exception();
  test_async_scope_stopped();
}