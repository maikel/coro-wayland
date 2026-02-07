// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "coro_just.hpp"
#include "just_stopped.hpp"
#include "observables/single.hpp"
#include "observables/use_resource.hpp"
#include "observables/zip.hpp"
#include "sync_wait.hpp"
#include "wayland/Client.hpp"
#include "wayland/FrameBufferPool.hpp"
#include "wayland/WindowSurface.hpp"
#include "when_all.hpp"
#include "when_any.hpp"
#include "when_stop_requested.hpp"

#include "Text.hpp"
#include "wayland/Window.hpp"

using namespace cw;

auto coro_main(FontManager& fonts) -> IoTask<void> {
  Text helloWorld(fonts.get_default(), "Hallo, Welt!", 0xFF00FF00);
  [[maybe_unused]] Window window = co_await use_resource(Window::make(std::move(helloWorld)));
  co_await when_stop_requested();
}

int main() {
  FontManager fonts;
  sync_wait(coro_main(fonts));
}