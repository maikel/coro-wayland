// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "AsyncUnorderedMap.hpp"
#include "Logging.hpp"
#include "observables/use_resource.hpp"
#include "queries.hpp"
#include "read_env.hpp"
#include "sync_wait.hpp"
#include "wayland/Connection.hpp"
#include "wayland/protocol.hpp"
#include "when_all.hpp"
#include "when_any.hpp"

#include <iostream>

auto create_window(ms::wayland::Registry& registry,
                   ms::AsyncUnorderedMap<std::string, std::uint32_t>& nameFromInterface)
    -> ms::IoTask<void> {
  std::uint32_t compositorName =
      co_await nameFromInterface.wait_for(ms::wayland::Compositor::interface_name());
  ms::Log::i("Compositor name: {}", compositorName);
}

int main() {
  ms::wayland::Connection connection;
  ms::sync_wait(connection.run().subscribe([](auto handle) noexcept -> ms::IoTask<void> {
    ms::wayland::ConnectionHandle connHandle = co_await std::move(handle);
    ms::wayland::Display display = co_await ms::use_resource(
        ms::wayland::Display::make(ms::wayland::ObjectId::Display, connHandle));
    ms::wayland::Registry registry = co_await ms::use_resource(display.get_registry());
    ms::AsyncUnorderedMap<std::string, std::uint32_t> nameFromInterface{connHandle.get_scheduler()};
    auto handleEvents =
        registry.events().subscribe([&](auto eventTask) noexcept -> ms::IoTask<void> {
          auto event = co_await std::move(eventTask);
          switch (event.index()) {
          case ms::wayland::Registry::GlobalEvent::index: {
            const auto globalEvent = std::get<ms::wayland::Registry::GlobalEvent>(event);
            ms::Log::i("Global added: name={}, interface={}, version={}", globalEvent.name,
                       globalEvent.interface, globalEvent.version);
            co_await nameFromInterface.emplace(std::string{globalEvent.interface},
                                               globalEvent.name);
            break;
          }
          case ms::wayland::Registry::GlobalRemoveEvent::index: {
            const auto removeEvent = std::get<ms::wayland::Registry::GlobalRemoveEvent>(event);
            ms::Log::i("Global removed: name={}", removeEvent.name);
            break;
          }
          }
        });
    co_await ms::when_any(std::move(handleEvents), create_window(registry, nameFromInterface));
  }));
}