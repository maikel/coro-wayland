// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "AsyncChannel.hpp"

#include "when_all.hpp"
#include "sync_wait.hpp"

auto test_async_channel() -> cw::IoTask<void> {
  cw::AsyncChannel<int> channel = co_await cw::use_resource(cw::AsyncChannel<int>::make());

  std::vector<int> receivedValues;

  auto sendTask = [](cw::AsyncChannel<int> channel) -> cw::IoTask<void> {
    for (int i = 0; i < 5; ++i) {
      co_await channel.send(i);
    }
  }(channel);

  auto receiveTask = channel.receive().subscribe(
        [&](cw::IoTask<int> valueTask) -> cw::IoTask<void> {
          int value = co_await std::move(valueTask);
          receivedValues.push_back(value);
          if (receivedValues.size() >= 5) {
            co_await cw::just_stopped();
          }
        });

  co_await cw::when_all(std::move(sendTask), std::move(receiveTask));

  assert(receivedValues.size() == 5);
  assert((receivedValues == std::vector<int>{0, 1, 2, 3, 4}));
}

int main() {
    cw::sync_wait(test_async_channel());
}