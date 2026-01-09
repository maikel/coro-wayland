// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Observable.hpp"
#include <observables/empty.hpp>
#include <observables/single.hpp>

#include <sync_wait.hpp>

#include <cassert>

void test_construct_observable() {
  cw::Observable<int> obs = cw::observables::empty();
  int counter = 0;
  auto receiver = [&](cw::IoTask<int>&&) noexcept -> cw::IoTask<void> {
    ++counter;
    co_return;
  };
  cw::IoTask<void> task = std::move(obs).subscribe(std::move(receiver));
  assert(cw::sync_wait(std::move(task)));
  assert(counter == 0);
}

auto coro_just(int value) -> cw::IoTask<int> { co_return value; }

void test_construct_single_observable() {
  cw::Observable<int> obs = cw::observables::single(coro_just(42));
  int counter = 0;
  auto receiver = [&](cw::IoTask<int> task) noexcept -> cw::IoTask<void> {
    int value = co_await std::move(task);
    assert(value == 42);
    ++counter;
  };
  cw::IoTask<void> task = std::move(obs).subscribe(std::move(receiver));
  assert(cw::sync_wait(std::move(task)));
  assert(counter == 1);
}

int main() {
  test_construct_observable();
  test_construct_single_observable();
}