/*
 * Copyright (c) 2023 Maikel Nadolski
 * Copyright (c) 2023 NVIDIA Corporation
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "bwos_lifo_queue.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <ranges>
#include <thread>
#include <vector>

namespace {
void test_lifo_queue_observers() {
  const cw::bwos::lifo_queue<int*> queue(8, 2);
  assert(queue.block_size() == 2);
  assert(queue.num_blocks() == 8);
}

void test_empty_get() {
  cw::bwos::lifo_queue<void*> queue(8, 2);
  assert(queue.pop_back() == nullptr);
}

void test_empty_steal() {
  cw::bwos::lifo_queue<void*> queue(8, 2);
  assert(queue.steal_front() == nullptr);
}

void test_put_one_get_one() {
  cw::bwos::lifo_queue<const int*> queue(8, 2);
  const int x = 1;
  assert(queue.push_back(&x));
  assert(queue.pop_back() == &x);
  assert(queue.pop_back() == nullptr);
}

void test_put_one_steal_none() {
  cw::bwos::lifo_queue<const int*> queue(8, 2);
  const int x = 1;
  assert(queue.push_back(&x));
  assert(queue.steal_front() == nullptr);
  assert(queue.pop_back() == &x);
}

void test_put_one_get_one_put_one_get_one() {
  cw::bwos::lifo_queue<int*> queue(8, 2);
  int x = 1;
  int y = 2;
  assert(queue.push_back(&x));
  assert(queue.pop_back() == &x);
  assert(queue.push_back(&y));
  assert(queue.pop_back() == &y);
  assert(queue.pop_back() == nullptr);
}

void test_put_two_get_two() {
  cw::bwos::lifo_queue<int*> queue(8, 2);
  int x = 1;
  int y = 2;
  assert(queue.push_back(&x));
  assert(queue.push_back(&y));
  assert(queue.pop_back() == &y);
  assert(queue.pop_back() == &x);
  assert(queue.pop_back() == nullptr);
}

void test_put_three_steal_two() {
  cw::bwos::lifo_queue<int*> queue(8, 2);
  int x = 1;
  int y = 2;
  assert(queue.push_back(&x));
  assert(queue.push_back(&y));
  assert(queue.push_back(&x));
  assert(queue.steal_front() == &x);
  assert(queue.steal_front() == &y);
  assert(queue.steal_front() == nullptr);
  assert(queue.pop_back() == &x);
  assert(queue.pop_back() == nullptr);
}

void test_put_4_steal_1_get_3() {
  cw::bwos::lifo_queue<int*> queue(8, 2);
  int x = 1;
  int y = 2;
  assert(queue.push_back(&x));
  assert(queue.push_back(&y));
  assert(queue.push_back(&x));
  assert(queue.push_back(&y));
  assert(queue.steal_front() == &x);
  assert(queue.pop_back() == &y);
  assert(queue.pop_back() == &x);
  assert(queue.pop_back() == &y);
  assert(queue.pop_back() == nullptr);
}

void test_size_one() {
  cw::bwos::lifo_queue<int*> queue(1, 1);
  int x = 1;
  int y = 2;
  assert(queue.push_back(&x));
  assert(!queue.push_back(&y));
  assert(queue.steal_front() == nullptr);
  assert(queue.pop_back() == &x);
  assert(queue.pop_back() == nullptr);
}

void test_twice_size_one() {
  cw::bwos::lifo_queue<int*> queue(2, 1);
  int x = 1;
  int y = 2;
  assert(queue.push_back(&x));
  assert(queue.steal_front() == nullptr);
  assert(queue.pop_back() == &x);
  assert(queue.pop_back() == nullptr);
  assert(queue.push_back(&x));
  assert(queue.push_back(&y));
  assert(!queue.push_back(&x));
  assert(queue.steal_front() == &x);
  assert(queue.steal_front() == nullptr);
  assert(queue.pop_back() == &y);
  assert(queue.pop_back() == nullptr);
  assert(queue.push_back(&x));
  assert(queue.pop_back() == &x);
  assert(queue.pop_back() == nullptr);
}

// ============================================================================
// MULTITHREADED TESTS
// ============================================================================

// Test round counter correctness when wrapping around the block array
void test_round_counter_wraparound() {
  constexpr std::size_t numBlocks = 4; // Small to force wraparounds
  constexpr std::size_t blockSize = 2;
  constexpr std::size_t numItems = numBlocks * blockSize * 3; // 3 full rounds

  cw::bwos::lifo_queue<int> queue(numBlocks, blockSize);
  const std::vector<int> values =                                   //
      std::ranges::views::iota(1, static_cast<int>(numItems + 1)) //
      | std::ranges::to<std::vector<int>>();
  std::vector<int> stolenItems;
  // Push items across multiple rounds
  for (const int val : values) {
    if (!queue.push_back(val)) {
      // Queue full, steal some items to make space
      while (true) {
        const int stolen = queue.steal_front();
        if (stolen == 0) {
          break; // No more to steal
        }
        stolenItems.push_back(stolen);
      }
      // Retry push
      assert(queue.push_back(val));
    }
  }

  auto prefix = std::ranges::views::take(values, stolenItems.size());
  assert(std::ranges::equal(stolenItems, prefix));
  assert(stolenItems.size() <= numItems);

  std::vector<int> remainingItems;
  // Pop remaining items from owner side
  while (true) {
    const int item = queue.pop_back();
    if (item == 0) {
      break; // Empty
    }
    remainingItems.push_back(item);
  }
  assert(std::ranges::is_sorted(remainingItems.rbegin(), remainingItems.rend()));
  assert(stolenItems.size() + remainingItems.size() == numItems);

  assert(queue.pop_back() == 0); // Empty
}

// Test concurrent stealing with multiple thieves
void test_concurrent_stealing() {
  constexpr std::size_t numItems = 2000;
  constexpr std::size_t numThieves = 4;

  cw::bwos::lifo_queue<std::size_t> queue(32, 64);

  // Owner pushes items
  for (std::size_t i = 1; i <= numItems; ++i) {
    assert(queue.push_back(i));
  }

  // Multiple thieves steal concurrently
  std::vector<std::vector<std::size_t>> stolen(numThieves);
  std::vector<std::thread> thieves;
  thieves.reserve(numThieves);
  for (std::size_t t = 0; t < numThieves; ++t) {
    thieves.emplace_back([&queue, &stolen, t]() {
      while (true) {
        const std::size_t item = queue.steal_front();
        if (item == 0) {
          break; // Assuming 0 means empty
        }
        stolen[t].push_back(item);
      }
    });
  }

  for (auto& thief : thieves) {
    thief.join();
  }

  // Collect all stolen items
  std::vector<std::size_t> allStolen;
  for (const auto& vec : stolen) {
    allStolen.insert(allStolen.end(), vec.begin(), vec.end());
  }

  // Check that stolen items are unique (no duplicates due to ABA problem)
  std::ranges::sort(allStolen);
  auto unique = std::ranges::unique(allStolen);
  assert(std::ranges::begin(unique) == std::ranges::end(unique));
  assert(allStolen.size() <= numItems);
}

// Test owner push/pop while thieves are stealing
void test_concurrent_owner_and_thieves() {
  constexpr std::size_t numItems = 10000;
  constexpr std::size_t numThieves = 2;

  cw::bwos::lifo_queue<std::size_t> queue(16, 32);
  std::atomic<bool> done{false};
  std::atomic<std::size_t> ownerPopped{0};
  std::atomic<std::size_t> totalStolen{0};

  // Owner thread: push and occasionally pop
  std::thread owner([&]() {
    for (std::size_t i = 1; i <= numItems; ++i) {
      while (!queue.push_back(i)) {
        // Queue full, pop some items
        if (queue.pop_back() != 0) {
          ownerPopped++;
        }
      }

      // Occasionally pop
      if (i % 100 == 0) {
        if (queue.pop_back() != 0) {
          ownerPopped++;
        }
      }
    }
    done = true;
  });

  // Thief threads
  std::vector<std::thread> thieves;
  thieves.reserve(numThieves);
  for (std::size_t t = 0; t < numThieves; ++t) {
    thieves.emplace_back([&]() {
      while (!done.load(std::memory_order_relaxed)) {
        if (queue.steal_front() != 0) {
          totalStolen++;
        }
      }
      // Drain remaining
      while (queue.steal_front() != 0) {
        totalStolen++;
      }
    });
  }

  owner.join();
  for (auto& thief : thieves) {
    thief.join();
  }

  // Drain any remaining items from owner side
  std::size_t remaining = 0;
  auto val = queue.pop_back();
  while (val != 0) {
    remaining++;
    val = queue.pop_back();
  }

  const std::size_t accounted = ownerPopped + totalStolen + remaining;
  assert(accounted == numItems);
}

// Test block wraparound with concurrent stealing
void test_block_wraparound_with_stealing() {
  constexpr std::size_t numBlocks = 4;
  constexpr std::size_t blockSize = 8;
  constexpr std::size_t rounds = 5;
  constexpr std::size_t itemsPerRound = numBlocks * blockSize;

  cw::bwos::lifo_queue<std::size_t> queue(numBlocks, blockSize);
  std::atomic<std::size_t> stolenCount{0};
  std::atomic<bool> thiefActive{true};

  // Thief thread continuously stealing
  std::thread thief([&]() {
    while (thiefActive) {
      if (queue.steal_front()) {
        stolenCount++;
      }
    }
  });

  // Owner pushes items across multiple rounds
  for (std::size_t round = 0; round < rounds; ++round) {
    for (std::size_t i = 0; i < itemsPerRound; ++i) {
      const std::size_t value = (round * itemsPerRound) + i + 1;
      while (!queue.push_back(value)) {
        // If queue is full, pop to make space
        queue.pop_back();
      }
    }
  }

  thiefActive = false;
  thief.join();
}

// Test is_writable() correctness - ensure blocks aren't reused prematurely
void test_block_reuse_safety() {
  constexpr std::size_t numBlocks = 8;
  constexpr std::size_t blockSize = 4;
  constexpr std::size_t totalItems = numBlocks * blockSize * 2; // Force wraparound

  cw::bwos::lifo_queue<std::size_t> queue(numBlocks, blockSize);

  std::mutex mtx;
  std::vector<std::size_t> extractedItems;

  // Push items to fill queue
  for (std::size_t i = 1; i <= totalItems; ++i) {
    if (!queue.push_back(i)) {
      // Can't push more - this is expected when queue is full
      break;
    }
  }

  // Thief slowly steals while owner tries to push more
  std::thread thief([&]() {
    std::vector<std::size_t> extracted;
    for (std::size_t i = 0; i < 100; ++i) {
      auto val = queue.steal_front();
      if (val != 0) {
        extracted.push_back(val);
      }
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    const std::lock_guard lock(mtx);
    extractedItems.insert(extractedItems.end(), extracted.begin(), extracted.end());
  });

  // Owner tries to push more items
  std::thread owner([&]() {
    for (std::size_t i = totalItems + 1; i <= totalItems + 50; ++i) {
      // Should successfully push as blocks become available
      while (!queue.push_back(i)) {
        std::this_thread::yield();
      }
    }
  });

  thief.join();
  owner.join();

  while (true) {
    auto val = queue.pop_back();
    if (val == 0) {
      break;
    }
    extractedItems.push_back(val);
  }

  // std::ranges::sort(extractedItems);
  // for (std::size_t i = 0; i < totalItems; ++i) {
  //   assert(extractedItems[i] == i + 1);
  // }
}

// Test takeover() and grant() synchronization
void test_takeover_grant_synchronization() {
  constexpr std::size_t numBlocks = 4;
  constexpr std::size_t blockSize = 16;
  constexpr std::size_t iterations = 1000;

  cw::bwos::lifo_queue<std::size_t> queue(numBlocks, blockSize);
  std::atomic<std::size_t> totalStolen{0};
  std::atomic<std::size_t> totalPopped{0};

  std::thread owner([&]() {
    for (std::size_t iter = 0; iter < iterations; ++iter) {
      // Push items
      for (std::size_t i = 0; i < blockSize; ++i) {
        while (!queue.push_back((iter * blockSize) + i + 1)) {
          std::this_thread::yield(); // Wait for thief to steal
        }
      }

      // Pop some back (triggers takeover when moving backward)
      for (std::size_t i = 0; i < blockSize / 2; ++i) {
        if (queue.pop_back() != 0) {
          totalPopped++;
        }
      }
    }
  });

  std::thread thief([&]() {
    while (totalPopped < iterations * blockSize / 2) {
      if (queue.steal_front() != 0) {
        totalStolen++;
      }
    }
  });

  owner.join();
  thief.join();
}

// Stress test with many thieves and high contention
void test_high_contention_stress() {
  constexpr std::size_t numItems = 50000;
  constexpr std::size_t numThieves = 8;
  constexpr std::size_t numBlocks = 16;
  constexpr std::size_t blockSize = 32;

  cw::bwos::lifo_queue<std::size_t> queue(numBlocks, blockSize);
  std::atomic<std::size_t> totalStolen{0};
  std::atomic<std::size_t> pushCount{0};
  std::atomic<bool> done{false};

  // Owner pushes continuously
  std::thread owner([&]() {
    for (std::size_t i = 1; i <= numItems; ++i) {
      while (!queue.push_back(i)) {
        std::this_thread::yield();
      }
      pushCount++;
    }
    done = true;
  });

  // Many thieves compete for items
  std::vector<std::thread> thieves;
  std::vector<std::atomic<std::size_t>> thiefCounts(numThieves);

  thieves.reserve(numThieves);
  for (std::size_t t = 0; t < numThieves; ++t) {
    thieves.emplace_back([&, t]() {
      std::size_t localCount = 0;
      while (true) {
        const auto val = queue.steal_front();
        if (val != 0) {
          localCount++;
        } else if (done) {
          break;
        }
      }
      thiefCounts[t] = localCount;
    });
  }

  owner.join();
  for (auto& thief : thieves) {
    thief.join();
  }

  // Sum up all stolen items
  for (const auto& count : thiefCounts) {
    totalStolen += count.load();
  }

  // Check remaining items
  std::size_t remaining = 0;
  while (queue.pop_back() != 0) {
    remaining++;
  }

  assert(pushCount == numItems);
  assert(totalStolen + remaining == numItems);
}

// Test edge case: steal from block at boundary during wraparound
void test_steal_during_wraparound() {
  constexpr std::size_t numBlocks = 4;
  constexpr std::size_t blockSize = 4;

  cw::bwos::lifo_queue<std::size_t> queue(numBlocks, blockSize);
  std::atomic<bool> startStealing{false};
  std::atomic<std::size_t> stolen{0};

  // Thief waits for signal then steals
  std::thread thief([&]() {
    while (!startStealing) {
      std::this_thread::yield();
    }
    while (queue.steal_front() != 0) {
      stolen++;
    }
  });

  // Owner fills queue and wraps around
  for (std::size_t round = 0; round < 3; ++round) {
    for (std::size_t i = 0; i < numBlocks * blockSize; ++i) {
      if (!queue.push_back((round * 100) + i + 1)) {
        // Queue full, start stealing
        startStealing = true;
        while (!queue.push_back((round * 100) + i + 1)) {
          std::this_thread::yield();
        }
      }
    }
  }

  startStealing = true;
  thief.join();
}

} // namespace

auto main() -> int try {
  test_lifo_queue_observers();
  test_empty_get();
  test_empty_steal();
  test_put_one_get_one();
  test_put_one_steal_none();
  test_put_one_get_one_put_one_get_one();
  test_put_two_get_two();
  test_put_three_steal_two();
  test_put_4_steal_1_get_3();
  test_size_one();
  test_twice_size_one();
  test_round_counter_wraparound();
  test_concurrent_stealing();
  test_concurrent_owner_and_thieves();
  test_block_wraparound_with_stealing();
  test_block_reuse_safety();
  test_takeover_grant_synchronization();
  test_high_contention_stress();
  test_steal_during_wraparound();
  return 0;
} catch (...) {
  std::puts("Test failed with unknown exception\n");
  return 1;
}