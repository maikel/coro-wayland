// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Logging.hpp"
#include "queries.hpp"
#include "read_env.hpp"
#include "sync_wait.hpp"
#include "wayland/Connection.hpp"
#include "wayland/protocol.hpp"

#include <iostream>

int main() {
  ms::wayland::Connection connection;
  ms::sync_wait(connection.run().subscribe([](auto handle) noexcept -> ms::IoTask<void> {
    ms::wayland::ConnectionHandle connHandle = co_await std::move(handle);
    ms::wayland::Display display{ms::wayland::ObjectId::Display, connHandle};
    ms::wayland::Registry registry = display.get_registry();
    auto scope = ms::create_scope().subscribe([&](auto) noexcept -> ms::IoTask<void> {
      co_await registry.events().subscribe([](auto eventTask) noexcept -> ms::IoTask<void> {
        auto event = co_await std::move(eventTask);
        switch (event.index()) {
        case ms::wayland::Registry::GlobalEvent::index: {
          const auto& globalEvent = std::get<ms::wayland::Registry::GlobalEvent>(event);
          ms::Log::i("Global added: name={}, interface={}, version={}", globalEvent.name,
                     globalEvent.interface, globalEvent.version);
          break;
        }
        case ms::wayland::Registry::GlobalRemoveEvent::index: {
          const auto& removeEvent = std::get<ms::wayland::Registry::GlobalRemoveEvent>(event);
          ms::Log::i("Global removed: name={}", removeEvent.name);
          break;
        }
        }
        co_return;
      });
    });
    co_await std::move(scope);
  }));
}