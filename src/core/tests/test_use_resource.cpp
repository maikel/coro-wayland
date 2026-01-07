// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "observables/use_resource.hpp"

//
#include "just_stopped.hpp"
#include "queries.hpp"
#include "read_env.hpp"
#include "sync_wait.hpp"

#include <cassert>

auto coro_push_back(std::vector<int>& vec, int value) -> ms::Task<void> {
  vec.push_back(value);
  co_return;
}

struct TestResource {
  auto subscribe(auto receiver) -> ms::IoTask<void> {
    return [](std::vector<int>* vec, int value, auto receiver) -> ms::IoTask<void> {
      ms::IoTask<int> task = [](int value) -> ms::IoTask<int> { co_return value; }(value);
      co_await coro_push_back(*vec, 2 * value - 1);
      co_await receiver(std::move(task));
      co_await coro_push_back(*vec, 2 * value);
    }(mVec, mValue, std::move(receiver));
  }

  std::vector<int>* mVec;
  int mValue;
};

auto coro_use_resource(std::vector<int>& vec, int value) -> ms::IoTask<void> {
  ms::Observable<int> valueResource1{TestResource{&vec, value + 1}};
  ms::Observable<int> valueResource2{TestResource{&vec, value}};
  int val1 = co_await ms::use_resource(std::move(valueResource1));
  int val2 = co_await ms::use_resource(std::move(valueResource2));
  co_await coro_push_back(vec, val1);
  co_await coro_push_back(vec, val2);
}

auto coro_use_resource_exception(std::vector<int>& vec, int value) -> ms::IoTask<void> {
  ms::Observable<int> valueResource{TestResource{&vec, value}};
  int val = co_await ms::use_resource(std::move(valueResource));
  co_await coro_push_back(vec, val);
  throw std::runtime_error("Test exception");
}

auto test_use_resource() -> void {
  std::vector<int> vec{};
  bool success = ms::sync_wait(coro_use_resource(vec, 42));
  assert(success);
  assert((vec == std::vector<int>{85, 83, 43, 42, 84, 86}));
}

auto test_use_resource_exception() -> void {
  std::vector<int> vec{};
  bool exception_caught = false;
  try {
    ms::sync_wait(coro_use_resource_exception(vec, 42));
    assert(false); // Should not reach here
  } catch (const std::runtime_error& e) {
    exception_caught = true;
    assert(std::string(e.what()) == "Test exception");
  }
  assert(exception_caught);
  assert((vec == std::vector<int>{83, 42, 84}));
}

int main() {
  test_use_resource();
  test_use_resource_exception();
}