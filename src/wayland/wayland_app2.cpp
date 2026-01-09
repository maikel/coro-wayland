// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "observables/use_resource.hpp"
#include "sync_wait.hpp"
#include "wayland/Client.hpp"
#include "wayland/FrameBufferPool.hpp"
#include "wayland/WindowSurface.hpp"
#include "when_stop_requested.hpp"

#include "narrow.hpp"

#include "Logging.hpp"

using namespace cw;

auto coro_main() -> IoTask<void> {
  Client client = co_await use_resource(Client::make());
  FrameBufferPool frameBufferPool = co_await use_resource(FrameBufferPool::make(client));
  WindowSurface windowSurface = co_await use_resource(WindowSurface::make(client));

  Log::i("Wayland resources acquired successfully.");
  auto configureFrameBuffer = windowSurface.configure_events().subscribe(
      [&](IoTask<protocol::XdgToplevel::ConfigureEvent> eventTask) -> IoTask<void> {
        auto event = co_await std::move(eventTask);
        Log::i("Received WindowSurface::ConfigureEvent with size {}x{}", event.width, event.height);
        co_await frameBufferPool.resize(Width{narrow<std::size_t>(event.width)},
                                        Height{narrow<std::size_t>(event.height)});
        Log::i("Resized FrameBufferPool to {}x{}", event.width, event.height);
        windowSurface.attach(frameBufferPool.get_current_buffers()[0].buffer, 0, 0);
      });

  co_await std::move(configureFrameBuffer);
}

int main() { sync_wait(coro_main()); }