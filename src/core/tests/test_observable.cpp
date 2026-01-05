// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Observable.hpp"
#include <observables/empty.hpp>
#include <observables/single.hpp>

#include <sync_wait.hpp>

#include <cassert>

void test_construct_observable() {
  ms::Observable<int> obs = ms::observables::empty();
  int counter = 0;
  auto receiver = [&](ms::IoTask<int>&&) noexcept -> ms::IoTask<void> {
    ++counter;
    co_return;
  };
  ms::IoTask<void> task = std::move(obs).subscribe(std::move(receiver));
  assert(ms::sync_wait(std::move(task)));
  assert(counter == 0);
}

auto coro_just(int value) -> ms::IoTask<int> { co_return value; }

void test_construct_single_observable() {
  ms::Observable<int> obs = ms::observables::single(coro_just(42));
  int counter = 0;
  auto receiver = [&](ms::IoTask<int> task) noexcept -> ms::IoTask<void> {
    int value = co_await std::move(task);
    assert(value == 42);
    ++counter;
  };
  ms::IoTask<void> task = std::move(obs).subscribe(std::move(receiver));
  assert(ms::sync_wait(std::move(task)));
  assert(counter == 1);
}

int main() {
  test_construct_observable();
  test_construct_single_observable();
}