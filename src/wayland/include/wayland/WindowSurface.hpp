// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "wayland/Client.hpp"
#include "wayland/XdgShell.hpp"

namespace cw {

struct WindowSurfaceContext;

class WindowSurface {
public:
  static auto make(Client client) -> Observable<WindowSurface>;

  auto configure_events() -> Observable<protocol::XdgToplevel::ConfigureEvent>;

  auto attach(protocol::Buffer buffer, std::int32_t x, std::int32_t y) -> void;

private:
  friend struct WindowSurfaceContext;
  explicit WindowSurface(WindowSurfaceContext& context) noexcept : mContext(&context) {}
  WindowSurfaceContext* mContext;
};

} // namespace cw