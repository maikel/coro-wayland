// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "wayland/WindowSurface.hpp"

#include "AsyncChannel.hpp"
#include "AsyncQueue.hpp"
#include "just_stopped.hpp"
#include "narrow.hpp"
#include "stopped_as_optional.hpp"
#include "wayland/XdgShell.hpp"
#include "when_any.hpp"
#include "when_stop_requested.hpp"

#include <stop_token>

namespace cw {

struct WindowSurfaceContext {
  Client mClient;
  protocol::Compositor mCompositor;
  protocol::Surface mSurface;
  AsyncChannel<protocol::XdgToplevel::ConfigureBoundsEvent> mConfigureBoundsChannel;
  AsyncChannel<protocol::XdgToplevel::ConfigureEvent> mConfigureChannel;
  AsyncChannel<protocol::XdgToplevel::CloseEvent> mCloseChannel;
  std::stop_source mStopSource{};

  auto receive_configure_bounds_events()
      -> Observable<protocol::XdgToplevel::ConfigureBoundsEvent> {
    return mConfigureBoundsChannel.receive();
  }

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

      using ConfigureQueue = AsyncQueue<
          std::variant<protocol::XdgSurface::ConfigureEvent, protocol::XdgToplevel::ConfigureEvent,
                       protocol::XdgToplevel::ConfigureBoundsEvent>>;

      using ConfigureChannel = AsyncChannel<
          std::variant<protocol::XdgSurface::ConfigureEvent, protocol::XdgToplevel::ConfigureEvent,
                       protocol::XdgToplevel::ConfigureBoundsEvent>>;

      auto configureBounds =
          co_await use_resource(AsyncChannel<protocol::XdgToplevel::ConfigureBoundsEvent>::make());

      auto configureBuffer =
          co_await use_resource(AsyncChannel<protocol::XdgToplevel::ConfigureEvent>::make());

      auto closeChannel =
          co_await use_resource(AsyncChannel<protocol::XdgToplevel::CloseEvent>::make());

      ConfigureChannel configureChannel = co_await use_resource(ConfigureChannel::make());
      ConfigureQueue configureQueue = co_await use_resource(ConfigureQueue::make());

      auto drainQueue = configureQueue.observable().subscribe([&](auto eventTask) -> IoTask<void> {
        auto event = co_await std::move(eventTask);
        co_await configureChannel.send(event);
      });

      WindowSurfaceContext context{client,          compositor,      surface,
                                   configureBounds, configureBuffer, closeChannel};

      auto pingEvent = xdgWmBase.events().subscribe(
          [&](IoTask<std::variant<protocol::XdgWmBase::PingEvent>> eventTask) -> IoTask<void> {
            auto event = std::get<0>(co_await std::move(eventTask));
            xdgWmBase.pong(event.serial);
          });

      auto configureSurface = xdgSurface.events().subscribe(
          [&](IoTask<std::variant<protocol::XdgSurface::ConfigureEvent>> eventTask)
              -> IoTask<void> {
            auto event = std::get<0>(co_await std::move(eventTask));
            co_await configureQueue.push(event);
          });

      auto configureTopLevel = xdgTopLevel.events().subscribe([&](auto eventTask) -> IoTask<void> {
        auto event = co_await std::move(eventTask);
        switch (event.index()) {
        case protocol::XdgToplevel::ConfigureEvent::index: {
          co_await configureQueue.push(std::get<protocol::XdgToplevel::ConfigureEvent>(event));
          break;
        }
        case protocol::XdgToplevel::CloseEvent::index: {
          co_await closeChannel.send(std::get<protocol::XdgToplevel::CloseEvent>(event));
          break;
        }
        case protocol::XdgToplevel::ConfigureBoundsEvent::index: {
          co_await configureQueue.push(
              std::get<protocol::XdgToplevel::ConfigureBoundsEvent>(event));
          break;
        }
        case protocol::XdgToplevel::WmCapabilitiesEvent::index: {
          break;
        }
        default:
          break;
        }
      });

      auto seatEvents = seat.events().subscribe([&](auto eventTask) -> IoTask<void> {
        auto event = co_await std::move(eventTask);
        switch (event.index()) {
        case protocol::Seat::CapabilitiesEvent::index: {
          break;
        }
        case protocol::Seat::NameEvent::index: {
          break;
        }
        default:
          break;
        }
      });

      auto pointerEvents = pointer.events().subscribe([&](auto eventTask) -> IoTask<void> {
        auto event = co_await std::move(eventTask);
        switch (event.index()) {
        case protocol::Pointer::MotionEvent::index: {
          break;
        }
        case protocol::Pointer::ButtonEvent::index: {
          break;
        }
        default:
          break;
        }
      });

      WindowSurface windowSurface = context.get_window_surface();

      xdgTopLevel.set_title("Wayland Window");

      surface.commit();

      auto configureEvents = configureChannel.receive().subscribe([&](auto eventTask)
                                                                      -> IoTask<void> {
        auto event = co_await std::move(eventTask);
        co_await std::visit(
            Overloaded{
                [&](protocol::XdgSurface::ConfigureEvent configureEvent) -> IoTask<void> {
                  xdgSurface.ack_configure(configureEvent.serial);
                  co_return;
                },
                [&](protocol::XdgToplevel::ConfigureEvent configureEvent) -> IoTask<void> {
                  co_await configureBuffer.send(configureEvent);
                },
                [&](protocol::XdgToplevel::ConfigureBoundsEvent configureBoundsEvent)
                    -> IoTask<void> {
                  co_await configureBounds.send(configureBoundsEvent);
                }},
            event);
      });

      co_await when_any(receiver(coro_just(windowSurface)), std::move(drainQueue),
                        std::move(pingEvent), std::move(configureSurface),
                        std::move(configureTopLevel), std::move(seatEvents),
                        std::move(pointerEvents), std::move(configureEvents),
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

auto WindowSurface::configure_bounds_events()
    -> Observable<protocol::XdgToplevel::ConfigureBoundsEvent> {
  return mContext->receive_configure_bounds_events();
}

auto WindowSurface::configure_events() -> Observable<protocol::XdgToplevel::ConfigureEvent> {
  return mContext->receive_configure_events();
}

auto WindowSurface::attach(protocol::Buffer buffer) -> void {
  mContext->mSurface.attach(buffer, 0, 0);
}

auto WindowSurface::damage(Region region) -> void {
  mContext->mSurface.damage_buffer(
      narrow<std::int32_t>(region.position.x), narrow<std::int32_t>(region.position.y),
      narrow<std::int32_t>(region.size.extent(0)), narrow<std::int32_t>(region.size.extent(1)));
}

auto WindowSurface::close_events() -> Observable<protocol::XdgToplevel::CloseEvent> {
  return mContext->receive_close_events();
}

auto WindowSurface::commit() -> void { mContext->mSurface.commit(); }

auto WindowSurface::frame() -> IoTask<void> {
  cw::protocol::Callback callback = co_await use_resource(mContext->mSurface.frame());
  co_await stopped_as_optional(callback.events().subscribe(
      [&](auto /* eventTask */) -> IoTask<void> { co_await just_stopped(); }));
}

} // namespace cw