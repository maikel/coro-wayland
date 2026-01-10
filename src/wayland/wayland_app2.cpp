// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "observables/use_resource.hpp"
#include "sync_wait.hpp"
#include "wayland/Client.hpp"
#include "wayland/FrameBufferPool.hpp"
#include "wayland/WindowSurface.hpp"
#include "when_stop_requested.hpp"

#include "Font.hpp"
#include "FontManager.hpp"
#include "GlyphCache.hpp"
#include "TextRenderer.hpp"

#include "narrow.hpp"

#include "Logging.hpp"

using namespace cw;

auto coro_main(GlyphCache& glyphCache, Font& font) -> IoTask<void> {
  Client client = co_await use_resource(Client::make());
  FrameBufferPool frameBufferPool = co_await use_resource(FrameBufferPool::make(client));
  WindowSurface windowSurface = co_await use_resource(WindowSurface::make(client));

  auto configureFrameBuffer = windowSurface.configure_events().subscribe(
      [&](IoTask<protocol::XdgToplevel::ConfigureEvent> eventTask) -> IoTask<void> {
        auto event = co_await std::move(eventTask);
        co_await frameBufferPool.resize(Width{narrow<std::size_t>(event.width)},
                                        Height{narrow<std::size_t>(event.height)});
        auto [buffer, pixels] = frameBufferPool.get_current_buffers()[0];
        TextRenderer textRenderer(glyphCache);
        auto textSize = textRenderer.measure_text(font, "Hello World!");
        textRenderer.draw_text(pixels, font, "Hello World!",
                               (pixels.extent(1) - textSize.width) / 2,
                               (pixels.extent(0) - textSize.height) / 2, Color{0, 255, 0, 255});
        windowSurface.attach(buffer, 0, 0);
      });

  co_await std::move(configureFrameBuffer);
}

int main() {
  FontManager fontManager;
  GlyphCache glyphCache;
  Font font = fontManager.load_font("DejaVu Sans Mono", 24);
  try {
    fontManager.add_font_directory("/home/nadolsm/Development/musicstreamer/assets");
    font = fontManager.load_font("PressStart2P-Regular", 24);
  } catch (FontManagerError const& e) {
    Log::w("Failed to load font: {}", e.what());
  }
  sync_wait(coro_main(glyphCache, font));
}