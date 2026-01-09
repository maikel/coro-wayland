// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "observables/use_resource.hpp"
#include "sync_wait.hpp"
#include "wayland/Client.hpp"
#include "wayland/FrameBufferPool.hpp"
#include "when_stop_requested.hpp"

#include "Logging.hpp"

using namespace cw;

auto coro_main() -> IoTask<void> {
  Client client = co_await use_resource(Client::make());
  FrameBufferPool frameBufferPool = co_await use_resource(FrameBufferPool::make(client));

  co_await frameBufferPool.get_current_buffers().subscribe(
      [&](IoTask<std::array<FrameBufferPool::BufferView, 2>> buffersTask) -> IoTask<void> {
        auto buffers = co_await std::move(buffersTask);
        for (auto& bufferView : buffers) {
          Log::i("Got buffer with id {:04X}",
                 std::to_underlying(bufferView.buffer.get_object_id()));
          Log::i("Buffer size: {}x{}", bufferView.pixels.extent(0), bufferView.pixels.extent(1));
        }
      });

  co_await cw::when_stop_requested();
}

int main() { sync_wait(coro_main()); }