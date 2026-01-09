// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "wayland/Client.hpp"

namespace cw {

struct WindowBaseContext;

class WindowBase {
public:
  static auto make(Client client) -> Observable<WindowBase>;

private:
  friend struct WindowBaseContext;
  explicit WindowBase(WindowBaseContext& context) noexcept : mContext(&context) {}
  WindowBaseContext* mContext;
};

} // namespace cw