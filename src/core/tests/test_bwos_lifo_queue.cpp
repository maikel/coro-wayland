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

void test_lifo_queue_observers() {
  cw::bwos::lifo_queue<int*> queue(8, 2);
  int x = 1;
  int y = 2;
  assert(queue.block_size() == 2);
  assert(queue.num_blocks() == 8);
}

void test_Empty_Get() {
  cw::bwos::lifo_queue<int*> queue(8, 2);
  int x = 1;
  int y = 2;
  assert(queue.pop_back() == nullptr);
}

void test_Empty_Steal() {
  cw::bwos::lifo_queue<int*> queue(8, 2);
  int x = 1;
  int y = 2;
  assert(queue.steal_front() == nullptr);
}

void test_Put_one_get_one() {
  cw::bwos::lifo_queue<int*> queue(8, 2);
  int x = 1;
  int y = 2;
  assert(queue.push_back(&x));
  assert(queue.pop_back() == &x);
  assert(queue.pop_back() == nullptr);
}

void test_Put_one_steal_none() {
  cw::bwos::lifo_queue<int*> queue(8, 2);
  int x = 1;
  int y = 2;
  assert(queue.push_back(&x));
  assert(queue.steal_front() == nullptr);
  assert(queue.pop_back() == &x);
}

void test_Put_one_get_one_put_one_get_one() {
  cw::bwos::lifo_queue<int*> queue(8, 2);
  int x = 1;
  int y = 2;
  assert(queue.push_back(&x));
  assert(queue.pop_back() == &x);
  assert(queue.push_back(&y));
  assert(queue.pop_back() == &y);
  assert(queue.pop_back() == nullptr);
}

void test_Put_two_get_two() {
  cw::bwos::lifo_queue<int*> queue(8, 2);
  int x = 1;
  int y = 2;
  assert(queue.push_back(&x));
  assert(queue.push_back(&y));
  assert(queue.pop_back() == &y);
  assert(queue.pop_back() == &x);
  assert(queue.pop_back() == nullptr);
}

void test_Put_three_Steal_two() {
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

void test_Put_4_Steal_1_Get_3() {
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

int main() {
  test_lifo_queue_observers();
  test_Empty_Get();
  test_Empty_Steal();
  test_Put_one_get_one();
  test_Put_one_steal_none();
  test_Put_one_get_one_put_one_get_one();
  test_Put_two_get_two();
  test_Put_three_Steal_two();
  test_Put_4_Steal_1_Get_3();
  test_size_one();
  test_twice_size_one();
}