// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "AsyncScope.hpp"
#include "StaticThreadPool.hpp"
#include "Task.hpp"
#include "sync_wait.hpp"
#include "when_all.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdio>

namespace {
void test_construct_and_destroy() { const cw::StaticThreadPool pool{1}; }

void test_sync_a_task() {
  cw::StaticThreadPool pool{1};
  const bool executed = cw::sync_wait(pool.schedule());
  assert(executed);
}

void test_sync_two_tasks() {
  cw::StaticThreadPool pool{2};
  const auto result = cw::sync_wait(cw::when_all(pool.schedule(), pool.schedule()));
  assert(result.has_value());
}

void test_schedule_bulk_one_worker() {
  cw::StaticThreadPool pool{1};
  const std::size_t count = 1'000;
  std::atomic<std::size_t> counter{0};
  const auto bulkSender = pool.schedule_bulk(count, [&](std::size_t) -> cw::Task<void> {
    counter.fetch_add(1, std::memory_order_relaxed);
    co_return;
  });
  cw::sync_wait(bulkSender);
  assert(counter.load(std::memory_order_relaxed) == count);
}

void test_schedule_bulk_four_workers() {
  cw::StaticThreadPool pool{4, cw::BwosParams{.numBlocks = 8, .blockSize = 32}};
  const std::size_t count = 1'000;
  std::atomic<std::size_t> counter{0};
  const auto bulkSender = pool.schedule_bulk(count, [&](std::size_t /*i*/) -> cw::Task<void> {
    counter.fetch_add(1, std::memory_order_relaxed);
    co_return;
  });
  cw::sync_wait(bulkSender);
  assert(counter.load(std::memory_order_relaxed) == count);
}

void test_schedule_four_workers() {
  cw::StaticThreadPool pool{4};
  cw::AsyncScope scope;
  static constexpr std::size_t count = 1'000;
  std::atomic<std::size_t> counter{0};
  for (std::size_t i = 0; i < count; ++i) {
    scope.spawn(
        [](cw::StaticThreadPool* pool, std::atomic<std::size_t>& counter) -> cw::Task<void> {
          co_await pool->schedule();
          counter.fetch_add(1, std::memory_order_relaxed);
          co_return;
        }(&pool, counter));
  }
  cw::sync_wait(scope.close());
  assert(counter.load(std::memory_order_relaxed) == count);
}
} // namespace

auto main() -> int try {
  test_construct_and_destroy();
  test_sync_a_task();
  test_sync_two_tasks();
  test_schedule_bulk_one_worker();
  test_schedule_bulk_four_workers();
  test_schedule_four_workers();
} catch (...) {
  std::puts("Test failed with unknown exception\n");
  return 1;
}