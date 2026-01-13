// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "PixelsView.hpp"
#include "Widget.hpp"
#include "wayland/Client.hpp"

namespace cw {

class WindowContext;

class Window {
public:
  static auto make(std::unique_ptr<Widget> rootWidget) -> Observable<Window>;

private:
  explicit Window(WindowContext& context) noexcept : mContext(&context) {}
  WindowContext* mContext;
};

} // namespace cw