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

namespace cw::wayland {

struct ClientContext {
  Connection mConnection;
  Display mDisplay;
  Registry mRegistry;
  AsyncUnorderedMapHandle<std::string, Registry::GlobalEvent> mGlobals;
  AsyncQueueHandle<wayland::Display::ErrorEvent> mErrorEvents;

  auto get_client() -> Client { return Client{*this}; }
};

namespace {
class MakeObserver {
public:
  auto subscribe(std::function<auto(IoTask<Client>)->IoTask<void>> receiver) const noexcept
      -> IoTask<void> {
    Connection connection = co_await use_resource(Connection::make());
    Display display = co_await use_resource(Display::make(ObjectId::Display, connection));
    Registry registry = co_await use_resource(display.get_registry());
    auto globals =
        co_await use_resource(AsyncUnorderedMap<std::string, Registry::GlobalEvent>::make());
    auto errorEvents = co_await use_resource(AsyncQueue<wayland::Display::ErrorEvent>::make());

    ClientContext context{connection, display, registry, globals, errorEvents};

    auto displaySubscriber =
        [&](IoTask<std::variant<Display::ErrorEvent, Display::DeleteIdEvent>> eventTask)
        -> IoTask<void> {
      std::variant<Display::ErrorEvent, Display::DeleteIdEvent> event =
          co_await std::move(eventTask);
      switch (event.index()) {
      case Display::ErrorEvent::index: {
        wayland::Display::ErrorEvent errorEvent = std::get<Display::ErrorEvent>(event);
        Log::e("Wayland Display Error: object_id={:04X}, code={}, message=\"{}\"",
               std::to_underlying(errorEvent.object_id), errorEvent.code, errorEvent.message);
        co_await errorEvents.push(std::move(errorEvent));
        break;
      }
      case Display::DeleteIdEvent::index: {
        wayland::Display::DeleteIdEvent deleteIdEvent = std::get<Display::DeleteIdEvent>(event);
        Log::d("Wayland Display Delete ID Event: id={}", deleteIdEvent.id);
        break;
      }
      }
    };

    auto registrySubscriber =
        [&](IoTask<std::variant<Registry::GlobalEvent, Registry::GlobalRemoveEvent>> eventTask)
        -> IoTask<void> {
      std::variant<Registry::GlobalEvent, Registry::GlobalRemoveEvent> event =
          co_await std::move(eventTask);
      switch (event.index()) {
      case Registry::GlobalEvent::index: {
        Registry::GlobalEvent globalEvent = std::get<Registry::GlobalEvent>(event);
        Log::d("Wayland Registry Global Event: name={}, interface=\"{}\", version={}",
               globalEvent.name, globalEvent.interface, globalEvent.version);
        co_await globals.emplace(globalEvent.interface, std::move(globalEvent));
        break;
      }
      case Registry::GlobalRemoveEvent::index: {
        Registry::GlobalRemoveEvent removeEvent = std::get<Registry::GlobalRemoveEvent>(event);
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

auto Client::events() -> Observable<wayland::Display::ErrorEvent> {
  return mContext->mErrorEvents.observable();
}

auto Client::get_next_object_id() -> ObjectId { return mContext->mConnection.get_next_object_id(); }

auto Client::find_global(std::string_view interface) -> IoTask<Registry::GlobalEvent> {
  co_return co_await mContext->mGlobals.wait_for(std::string{interface});
}

auto Client::bind_global(const Registry::GlobalEvent& global, ObjectId new_id) -> void {
  mContext->mRegistry.bind(global.name, global.interface, global.version, new_id);
}

} // namespace cw::wayland