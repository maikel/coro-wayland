// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "wayland/Client.hpp"

namespace cw {

struct WindowSurfaceContext;

class WindowSurface {
public:
  static auto make(Client client) -> Observable<WindowSurface>;

private:
  friend struct WindowSurfaceContext;
  explicit WindowSurface(WindowSurfaceContext& context) noexcept : mContext(&context) {}
  WindowSurfaceContext* mContext;
};

} // namespace cw