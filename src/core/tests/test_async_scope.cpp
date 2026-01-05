// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "AsyncScope.hpp"
#include "just_stopped.hpp"
#include "sync_wait.hpp"

auto coro_void() -> ms::Task<void> { co_return; }

auto coro_exception() -> ms::Task<void> {
  throw std::runtime_error("Test exception");
  co_return;
}

auto coro_stopped() -> ms::Task<void> { co_await ms::just_stopped(); }

void test_async_scope_void() {
  ms::AsyncScope scope;
  scope.spawn(coro_void());
  ms::sync_wait(scope.close());
}

void test_async_scope_exception() {
  ms::AsyncScope scope;
  scope.spawn(coro_exception());
  ms::sync_wait(scope.close());
}

void test_async_scope_stopped() {
  ms::AsyncScope scope;
  scope.spawn(coro_stopped());
  ms::sync_wait(scope.close());
}

int main() {
  test_async_scope_void();
  test_async_scope_exception();
  test_async_scope_stopped();
}