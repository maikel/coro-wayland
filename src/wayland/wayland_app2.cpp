// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "just_stopped.hpp"
#include "observables/use_resource.hpp"
#include "sync_wait.hpp"
#include "wayland/Client.hpp"
#include "wayland/FrameBufferPool.hpp"
#include "wayland/WindowSurface.hpp"
#include "when_all.hpp"
#include "when_any.hpp"
#include "when_stop_requested.hpp"

#include "Font.hpp"
#include "FontManager.hpp"
#include "GlyphCache.hpp"
#include "TextRenderer.hpp"

#include "narrow.hpp"

#include "Logging.hpp"

using namespace cw;

auto fill(PixelsView pixels, std::uint32_t color) -> void {
  for (std::size_t y = 0; y < pixels.height(); ++y) {
    for (std::size_t x = 0; x < pixels.width(); ++x) {
      pixels[x, y] = color;
    }
  }
}

auto coro_main(GlyphCache& glyphCache, Font& font) -> IoTask<void> {
  Client client = co_await use_resource(Client::make());
  FrameBufferPool frameBufferPool = co_await use_resource(FrameBufferPool::make(client));
  WindowSurface windowSurface = co_await use_resource(WindowSurface::make(client));
  TextRenderer textRenderer(glyphCache);
  int count = 1;

  auto configureFrameBuffer =
      windowSurface.configure_events().subscribe([&](auto eventTask) -> IoTask<void> {
        auto event = co_await std::move(eventTask);
        co_await frameBufferPool.resize(Width{narrow<std::size_t>(event.width)},
                                        Height{narrow<std::size_t>(event.height)});
        auto [buffer, pixels] = frameBufferPool.get_current_buffers()[0];
        const std::string text = std::format("Hello World #{}", count++);
        auto textSize = textRenderer.measure_text(font, text);
        auto mid_x = (pixels.width() - textSize.extent(0)) / 2;
        auto mid_y = (pixels.height() - textSize.extent(1)) / 2;
        Position offset{narrow<std::size_t>(mid_x), narrow<std::size_t>(mid_y)};
        pixels = pixels.subview(offset, textSize);
        fill(pixels, 0xFF000000); // ARGB black background
        textRenderer.draw_text(pixels, font, text, Color{0, 255, 0, 255});
        windowSurface.attach(buffer);
        windowSurface.damage(offset, textSize);
      });

  auto closeEventHandler =
      windowSurface.close_events().subscribe([&](auto eventTask) -> IoTask<void> {
        co_await std::move(eventTask);
        Log::i("Window close event received, exiting...");
        co_await just_stopped();
      });

  co_await when_any(std::move(configureFrameBuffer), std::move(closeEventHandler));
}

int main() {
  FontManager fontManager;
  GlyphCache glyphCache;
  Font font = fontManager.load_font("DejaVu Sans Mono", 24);
  sync_wait(coro_main(glyphCache, font));
}