// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "PixelsView.hpp"
#include "wayland/Client.hpp"
#include "wayland/XdgShell.hpp"

namespace cw {

struct WindowSurfaceContext;

class WindowSurface {
public:
  static auto make(Client client) -> Observable<WindowSurface>;

  auto configure_bounds_events() -> Observable<protocol::XdgToplevel::ConfigureBoundsEvent>;

  auto configure_events() -> Observable<protocol::XdgToplevel::ConfigureEvent>;

  auto close_events() -> Observable<protocol::XdgToplevel::CloseEvent>;

  auto attach(protocol::Buffer buffer) -> void;

  auto damage(Region region) -> void;

  auto frame() -> IoTask<void>;

  auto commit() -> void;

private:
  friend struct WindowSurfaceContext;
  explicit WindowSurface(WindowSurfaceContext& context) noexcept : mContext(&context) {}
  WindowSurfaceContext* mContext;
};

} // namespace cw