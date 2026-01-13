// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "StaticThreadPool.hpp"
#include "AsyncScope.hpp"
#include "sync_wait.hpp"
#include "when_all.hpp"

void test_construct_and_destroy() {
  cw::StaticThreadPool pool{1};
}

void test_sync_a_task() {
  cw::StaticThreadPool pool{1};
  bool executed = cw::sync_wait(pool.schedule());
  assert(executed);
}

void test_sync_two_tasks() {
  cw::StaticThreadPool pool{2};
  auto result = cw::sync_wait(cw::when_all(pool.schedule(), pool.schedule()));
  assert(result.has_value());
}

void test_schedule_bulk_one_worker() {
  cw::StaticThreadPool pool{1};
  std::size_t count = 10000;
  std::atomic<std::size_t> counter{0};
  auto bulkSender = pool.schedule_bulk(
      count, [&](std::size_t) -> cw::Task<void> {
        counter.fetch_add(1, std::memory_order_relaxed);
        co_return;
      });
  cw::sync_wait(bulkSender);
  assert(counter.load(std::memory_order_relaxed) == count);
}

void test_schedule_bulk_four_workers() {
  cw::StaticThreadPool pool{4};
  std::vector<float> data(1'000'000, 1.0f);
  std::size_t count = 1'000'000;
  std::atomic<std::size_t> counter{0};
  auto bulkSender = pool.schedule_bulk(
      count, [&](std::size_t i) -> cw::Task<void> {
        float prev = i == 0 ? -data[i] : data[i-1];
        float next = i == count - 1 ? -data[i] : data[i+1];
        data[i] = data[i] + (next - prev);
        counter.fetch_add(1, std::memory_order_relaxed);
        co_return;
      });
  cw::sync_wait(bulkSender);
  assert(counter.load(std::memory_order_relaxed) == count);
}

void test_schedule_four_workers() {
  std::vector<float> data(1'000'000, 1.0f);
  cw::StaticThreadPool pool{4};
  cw::AsyncScope scope;
  static constexpr std::size_t count = 1'000'000;
  std::atomic<std::size_t> counter{0};
  for (std::size_t i = 0; i < count; ++i)
      scope.spawn([](cw::StaticThreadPool* pool, std::atomic<std::size_t>& counter, std::vector<float>& data, std::size_t i) -> cw::Task<void> {
        co_await pool->schedule();
        float prev = i == 0 ? -data[i] : data[i-1];
        float next = i == count - 1 ? -data[i] : data[i+1];
        data[i] = data[i] + (next - prev);
        counter.fetch_add(1, std::memory_order_relaxed);
        co_return;
      }(&pool, counter, data, i));
  cw::sync_wait(scope.close());
  assert(counter.load(std::memory_order_relaxed) == count);
}

int main() {
//   test_construct_and_destroy();
//   test_sync_a_task();
//   test_sync_two_tasks();
//   test_schedule_bulk_one_worker();
  test_schedule_bulk_four_workers();
//   test_schedule_four_workers();
}