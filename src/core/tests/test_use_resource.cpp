// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "observables/use_resource.hpp"

//
#include "IoTask.hpp"
#include "Task.hpp"
#include "Observable.hpp"
#include "coro_guard.hpp"
#include "just_stopped.hpp"
#include "sync_wait.hpp"

#include <cassert>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
auto coro_push_back(std::vector<int>& vec, int value) -> cw::Task<void> {
  vec.push_back(value);
  co_return;
}

struct TestResource {
  auto subscribe(auto receiver) -> cw::IoTask<void> {
    return [](std::vector<int>* vec, int value, auto receiver) -> cw::IoTask<void> {
      cw::IoTask<int> task = [](int value) -> cw::IoTask<int> { co_return value; }(value);
      co_await coro_push_back(*vec, (2 * value) - 1);
      co_await coro_guard(coro_push_back(*vec, 2 * value));
      co_await receiver(std::move(task));
    }(mVec, mValue, std::move(receiver));
  }

  std::vector<int>* mVec;
  int mValue;
};

auto coro_use_resource(std::vector<int>& vec, int value) -> cw::IoTask<void> {
  cw::Observable<int> valueResource1{TestResource{&vec, value + 1}};
  cw::Observable<int> valueResource2{TestResource{&vec, value}};
  int val1 = co_await cw::use_resource(std::move(valueResource1));
  int val2 = co_await cw::use_resource(std::move(valueResource2));
  co_await coro_push_back(vec, val1);
  co_await coro_push_back(vec, val2);
}

auto coro_use_resource_exception(std::vector<int>& vec, int value) -> cw::IoTask<void> {
  cw::Observable<int> valueResource1{TestResource{.mVec = &vec, .mValue = value + 1}};
  cw::Observable<int> valueResource2{TestResource{.mVec = &vec, .mValue = value}};
  const int val1 = co_await cw::use_resource(std::move(valueResource1));
  const int val2 = co_await cw::use_resource(std::move(valueResource2));
  co_await coro_push_back(vec, val1);
  throw std::runtime_error("Test exception");
  co_await coro_push_back(vec, val2);
}

auto coro_use_resource_stopped(std::vector<int>& vec, int value) -> cw::IoTask<void> {
  cw::Observable<int> valueResource1{TestResource{&vec, value + 1}};
  cw::Observable<int> valueResource2{TestResource{&vec, value}};
  int val1 = co_await cw::use_resource(std::move(valueResource1));
  int val2 = co_await cw::use_resource(std::move(valueResource2));
  co_await coro_push_back(vec, val1);
  co_await cw::just_stopped();
  co_await coro_push_back(vec, val2);
}

auto test_use_resource() -> void {
  std::vector<int> vec{};
  bool success = cw::sync_wait(coro_use_resource(vec, 42));
  assert(success);
  assert((vec == std::vector<int>{85, 83, 43, 42, 84, 86}));
}

auto test_use_resource_exception() -> void {
  std::vector<int> vec{};
  bool exception_caught = false;
  try {
    cw::sync_wait(coro_use_resource_exception(vec, 42));
    assert(false); // Should not reach here
  } catch (const std::runtime_error& e) {
    exception_caught = true;
    assert(std::string(e.what()) == "Test exception");
  }
  assert(exception_caught);
  assert((vec == std::vector<int>{85, 83, 43, 84, 86}));
}

auto test_use_resource_stopped() -> void {
  std::vector<int> vec{};
  bool success = cw::sync_wait(coro_use_resource_stopped(vec, 42));
  assert(!success);
  assert((vec == std::vector<int>{85, 83, 43, 84, 86}));
}
} // namespace

auto main() -> int try {
  test_use_resource();
  test_use_resource_exception();
  test_use_resource_stopped();
} catch (const std::exception& ex) {
  std::puts("Test failed with exception:\n");
  std::puts(ex.what());
} catch (...) {
  std::puts("Test failed with unknown exception\n");
}