// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "wayland/Client.hpp"
#include "AsyncUnorderedMap.hpp"
#include "coro_just.hpp"
#include "just_stopped.hpp"
#include "observables/use_resource.hpp"
#include "stopped_as_optional.hpp"
#include "when_any.hpp"

#include "Logging.hpp"

namespace cw {

struct ClientContext {
  Connection mConnection;
  protocol::Display mDisplay;
  protocol::Registry mRegistry;
  AsyncUnorderedMapHandle<std::string, protocol::Registry::GlobalEvent> mGlobals;
  AsyncQueueHandle<protocol::Display::ErrorEvent> mErrorEvents;

  auto get_client() -> Client { return Client{*this}; }
};

namespace {
class MakeObserver {
public:
  auto subscribe(std::function<auto(IoTask<Client>)->IoTask<void>> receiver) const noexcept
      -> IoTask<void> {
    Connection connection = co_await use_resource(Connection::make());
    protocol::Display display =
        co_await use_resource(protocol::Display::make(ObjectId::Display, connection));
    protocol::Registry registry = co_await use_resource(display.get_registry());
    auto globals = co_await use_resource(
        AsyncUnorderedMap<std::string, protocol::Registry::GlobalEvent>::make());
    auto errorEvents = co_await use_resource(AsyncQueue<protocol::Display::ErrorEvent>::make());
    ClientContext context{connection, display, registry, globals, errorEvents};

    auto displaySubscriber =
        [&](IoTask<std::variant<protocol::Display::ErrorEvent, protocol::Display::DeleteIdEvent>>
                eventTask) -> IoTask<void> {
      std::variant<protocol::Display::ErrorEvent, protocol::Display::DeleteIdEvent> event =
          co_await std::move(eventTask);
      switch (event.index()) {
      case protocol::Display::ErrorEvent::index: {
        protocol::Display::ErrorEvent errorEvent = std::get<protocol::Display::ErrorEvent>(event);
        Log::e("Wayland Display Error: object_id={:04X}, code={}, message=\"{}\"",
               std::to_underlying(errorEvent.object_id), errorEvent.code, errorEvent.message);
        co_await errorEvents.push(std::move(errorEvent));
        break;
      }
      case protocol::Display::DeleteIdEvent::index: {
        protocol::Display::DeleteIdEvent deleteIdEvent =
            std::get<protocol::Display::DeleteIdEvent>(event);
        Log::d("Wayland Display Delete ID Event: id={}", deleteIdEvent.id);
        break;
      }
      }
    };

    auto registrySubscriber =
        [&](IoTask<
            std::variant<protocol::Registry::GlobalEvent, protocol::Registry::GlobalRemoveEvent>>
                eventTask) -> IoTask<void> {
      std::variant<protocol::Registry::GlobalEvent, protocol::Registry::GlobalRemoveEvent> event =
          co_await std::move(eventTask);
      switch (event.index()) {
      case protocol::Registry::GlobalEvent::index: {
        protocol::Registry::GlobalEvent globalEvent =
            std::get<protocol::Registry::GlobalEvent>(event);
        Log::d("Wayland Registry Global Event: name={}, interface=\"{}\", version={}",
               globalEvent.name, globalEvent.interface, globalEvent.version);
        co_await globals.emplace(globalEvent.interface, std::move(globalEvent));
        break;
      }
      case protocol::Registry::GlobalRemoveEvent::index: {
        protocol::Registry::GlobalRemoveEvent removeEvent =
            std::get<protocol::Registry::GlobalRemoveEvent>(event);
        Log::d("Wayland Registry Global Remove Event: name={}", removeEvent.name);
        // Note: We don't have the interface name here to remove from globals.
        break;
      }
      }
    };

    IoTask<void> errorEventTask = display.events().subscribe(displaySubscriber);
    IoTask<void> registryEventTask = registry.events().subscribe(registrySubscriber);
    Client client = context.get_client();
    auto downstreamTask = when_any(receiver(coro_just(client)), std::move(errorEventTask),
                                   std::move(registryEventTask));

    std::exception_ptr exception = nullptr;
    bool stopped = false;
    try {
      stopped = !(co_await cw::stopped_as_optional(std::move(downstreamTask))).has_value();
    } catch (...) {
      Log::e("Caught exception in wayland::Client");
      exception = std::current_exception();
    }
    if (stopped) {
      Log::d("wayland::Client was stopped.");
      co_await cw::just_stopped();
    }
    if (exception) {
      std::rethrow_exception(exception);
    }
    Log::d("wayland::Client completed.");
  }
};
} // namespace

auto Client::make() -> Observable<Client> { return MakeObserver{}; }

auto Client::connection() -> Connection { return mContext->mConnection; }

auto Client::events() -> Observable<protocol::Display::ErrorEvent> {
  return mContext->mErrorEvents.observable();
}

auto Client::get_next_object_id() -> ObjectId { return mContext->mConnection.get_next_object_id(); }

auto Client::find_global(std::string_view interface) -> IoTask<protocol::Registry::GlobalEvent> {
  co_return co_await mContext->mGlobals.wait_for(std::string{interface});
}

auto Client::bind_global(const protocol::Registry::GlobalEvent& global, ObjectId new_id) -> void {
  mContext->mRegistry.bind(global.name, global.interface, global.version, new_id);
}

} // namespace cw