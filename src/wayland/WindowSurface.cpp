// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "wayland/WindowSurface.hpp"

#include "AsyncChannel.hpp"
#include "narrow.hpp"
#include "wayland/XdgShell.hpp"
#include "when_any.hpp"
#include "when_stop_requested.hpp"

#include "Logging.hpp"

#include <stop_token>

namespace cw {

struct WindowSurfaceContext {
  Client mClient;
  protocol::Compositor mCompositor;
  protocol::Surface mSurface;
  AsyncChannel<protocol::XdgToplevel::ConfigureEvent> mConfigureChannel;
  AsyncChannel<protocol::XdgToplevel::CloseEvent> mCloseChannel;
  std::stop_source mStopSource{};

  auto receive_configure_events() -> Observable<protocol::XdgToplevel::ConfigureEvent> {
    return mConfigureChannel.receive();
  }

  auto receive_close_events() -> Observable<protocol::XdgToplevel::CloseEvent> {
    return mCloseChannel.receive();
  }

  auto get_env() const {
    struct Env {
      const WindowSurfaceContext* mContext;

      auto query(get_stop_token_t) const noexcept -> std::stop_token {
        return mContext->mStopSource.get_token();
      }

      auto query(get_scheduler_t) const noexcept -> IoScheduler {
        return mContext->mClient.connection().get_scheduler();
      }
    };

    return Env{this};
  }

  auto get_window_surface() -> WindowSurface { return WindowSurface{*this}; }
};

auto WindowSurface::make(Client client) -> Observable<WindowSurface> {
  struct WindowSurfaceObservable {
    static auto do_subscribe(Client client,
                             std::function<auto(IoTask<WindowSurface>)->IoTask<void>> receiver)
        -> IoTask<void> {
      protocol::Compositor compositor = co_await use_resource(client.bind<protocol::Compositor>());
      protocol::Surface surface = co_await use_resource(compositor.create_surface());
      protocol::XdgWmBase xdgWmBase = co_await use_resource(client.bind<protocol::XdgWmBase>());
      protocol::XdgSurface xdgSurface = co_await use_resource(xdgWmBase.get_xdg_surface(surface));
      protocol::XdgToplevel xdgTopLevel = co_await use_resource(xdgSurface.get_toplevel());
      protocol::Seat seat = co_await use_resource(client.bind<protocol::Seat>());
      protocol::Pointer pointer = co_await use_resource(seat.get_pointer());

      using ConfigureChannel = AsyncChannel<std::variant<protocol::XdgSurface::ConfigureEvent,
                                                         protocol::XdgToplevel::ConfigureEvent>>;

      auto configureBuffer =
          co_await use_resource(AsyncChannel<protocol::XdgToplevel::ConfigureEvent>::make());

      auto closeChannel =
          co_await use_resource(AsyncChannel<protocol::XdgToplevel::CloseEvent>::make());

      ConfigureChannel configureChannel = co_await use_resource(ConfigureChannel::make());

      WindowSurfaceContext context{client, compositor, surface, configureBuffer, closeChannel};

      AsyncScopeHandle scope = co_await use_resource(AsyncScope::make());

      scope.spawn(
          xdgWmBase.events().subscribe(
              [&](IoTask<std::variant<protocol::XdgWmBase::PingEvent>> eventTask) -> IoTask<void> {
                auto event = std::get<0>(co_await std::move(eventTask));
                // Log::i("Received XdgWmBase::PingEvent with serial {}", event.serial);
                xdgWmBase.pong(event.serial);
                // Log::i("Sent XdgWmBase::Pong for serial {}", event.serial);
              }),
          context.get_env());

      scope.spawn(xdgSurface.events().subscribe(
                      [&](IoTask<std::variant<protocol::XdgSurface::ConfigureEvent>> eventTask)
                          -> IoTask<void> {
                        auto event = std::get<0>(co_await std::move(eventTask));
                        // Log::i("Received from QUEUE XdgSurface::ConfigureEvent with serial {}",
                        //        event.serial);
                        co_await configureChannel.send(event);
                      }),
                  context.get_env());

      scope.spawn( //
          xdgTopLevel.events().subscribe([&](auto eventTask) -> IoTask<void> {
            auto event = co_await std::move(eventTask);
            switch (event.index()) {
            case protocol::XdgToplevel::ConfigureEvent::index: {
              // Log::i("Received from QUEUE XdgToplevel::ConfigureEvent with size {}x{}",
              //        std::get<protocol::XdgToplevel::ConfigureEvent>(event).width,
              //        std::get<protocol::XdgToplevel::ConfigureEvent>(event).height);
              co_await configureChannel.send(
                  std::get<protocol::XdgToplevel::ConfigureEvent>(event));
              break;
            }
            case protocol::XdgToplevel::CloseEvent::index: {
              // Log::i("Received from QUEUE XdgToplevel::CloseEvent");
              co_await closeChannel.send(std::get<protocol::XdgToplevel::CloseEvent>(event));
              break;
            }
            case protocol::XdgToplevel::ConfigureBoundsEvent::index: {
              // Log::i("Received from QUEUE XdgToplevel::ConfigureBoundsEvent with size {}x{}",
              //        std::get<protocol::XdgToplevel::ConfigureBoundsEvent>(event).width,
              //        std::get<protocol::XdgToplevel::ConfigureBoundsEvent>(event).height);
              break;
            }
            case protocol::XdgToplevel::WmCapabilitiesEvent::index: {
              // Log::i("Received from QUEUE XdgToplevel::WmCapabilitiesEvent");
              break;
            }
            default:
              break;
            }
          }),
          context.get_env());

      scope.spawn( //
          seat.events().subscribe([&](auto eventTask) -> IoTask<void> {
            auto event = co_await std::move(eventTask);
            switch (event.index()) {
            case protocol::Seat::CapabilitiesEvent::index: {
              // auto capabilitiesEvent = std::get<protocol::Seat::CapabilitiesEvent>(event);
              // Log::i("Received Seat::CapabilitiesEvent with capabilities bitmask {}",
              //        capabilitiesEvent.capabilities);
              break;
            }
            case protocol::Seat::NameEvent::index: {
              // auto nameEvent = std::get<protocol::Seat::NameEvent>(event);
              // Log::i("Received Seat::NameEvent with name '{}'", nameEvent.name);
              break;
            }
            default:
              break;
            }
          }),
          context.get_env());

      scope.spawn( //
          pointer.events().subscribe([&](auto eventTask) -> IoTask<void> {
            auto event = co_await std::move(eventTask);
            switch (event.index()) {
            case protocol::Pointer::MotionEvent::index: {
              // auto motionEvent = std::get<protocol::Pointer::MotionEvent>(event);
              // Log::i("Received Pointer::MotionEvent at position ({}, {})",
              //        motionEvent.surface_x / 256, motionEvent.surface_y / 256);
              break;
            }
            case protocol::Pointer::ButtonEvent::index: {
              // auto buttonEvent = std::get<protocol::Pointer::ButtonEvent>(event);
              // Log::i("Received Pointer::ButtonEvent for button {} with state {}",
              //        buttonEvent.button, buttonEvent.state);
              break;
            }
            default:
              break;
            }
          }),
          context.get_env());

      WindowSurface windowSurface = context.get_window_surface();

      xdgTopLevel.set_title("Wayland Window");

      surface.commit();

      auto configureEvents =
          configureChannel.receive().subscribe([&](auto eventTask) -> IoTask<void> {
            auto event = co_await std::move(eventTask);
            if (auto configureEvent = std::get_if<protocol::XdgToplevel::ConfigureEvent>(&event)) {
              co_await configureBuffer.send(*configureEvent);
            } else {
              auto surfaceEvent = std::get<protocol::XdgSurface::ConfigureEvent>(event);
              xdgSurface.ack_configure(surfaceEvent.serial);
              surface.commit();
            }
          });

      co_await when_any(receiver(coro_just(windowSurface)), std::move(configureEvents),
                        upon_stop_requested( //
                            [&] {            //
                              context.mStopSource.request_stop();
                            }));
    }

    auto subscribe(std::function<auto(IoTask<WindowSurface>)->IoTask<void>> receiver) const noexcept
        -> IoTask<void> {
      return do_subscribe(mClient, std::move(receiver));
    }

    Client mClient;
  };
  return WindowSurfaceObservable{std::move(client)};
}

auto WindowSurface::configure_events() -> Observable<protocol::XdgToplevel::ConfigureEvent> {
  return mContext->receive_configure_events();
}

auto WindowSurface::attach(protocol::Buffer buffer) -> void {
  mContext->mSurface.attach(buffer, 0, 0);
}

auto WindowSurface::damage(Position position, Extents extents) -> void {
  mContext->mSurface.damage_buffer(
      narrow<std::int32_t>(position.x), narrow<std::int32_t>(position.y),
      narrow<std::int32_t>(extents.extent(0)), narrow<std::int32_t>(extents.extent(1)));
}

auto WindowSurface::close_events() -> Observable<protocol::XdgToplevel::CloseEvent> {
  return mContext->receive_close_events();
}

} // namespace cw