// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "wayland/Window.hpp"

#include "when_all.hpp"
#include "when_any.hpp"

#include "GlyphCache.hpp"
#include "RenderContext.hpp"
#include "TextRenderer.hpp"
#include "Widget.hpp"

#include "PixelsView.hpp"
#include "wayland/Client.hpp"
#include "wayland/FrameBufferPool.hpp"
#include "wayland/WindowSurface.hpp"

namespace cw {

class WindowContext {
public:
  Client mClient;
  FrameBufferPool mFrameBufferPool;
  WindowSurface mWindowSurface;
};

auto Window::make(std::unique_ptr<Widget> rootWidget) -> Observable<Window> {
  struct WindowObservable {
    static auto do_subscribe(std::unique_ptr<Widget> rootWidget,
                             std::function<auto(IoTask<Window>)->IoTask<void>> receiver)
        -> IoTask<void> {
      Client client = co_await use_resource(Client::make());
      FrameBufferPool frameBufferPool = co_await use_resource(FrameBufferPool::make(client));
      WindowSurface windowSurface = co_await use_resource(WindowSurface::make(client));
      WindowContext context{client, frameBufferPool, windowSurface};
      GlyphCache glyphCache{};
      TextRenderer textRenderer(glyphCache);

      auto redrawOnChange = rootWidget->dirty().subscribe([&](auto isDirty) -> IoTask<void> {
        co_await when_all(std::move(isDirty), windowSurface.frame());
        auto available = co_await frameBufferPool.available_buffer();
        RenderContext renderContext{available.pixels, textRenderer};
        auto regions = rootWidget->render(renderContext);
        windowSurface.attach(available.buffer);
        for (const auto& region : regions) {
          windowSurface.damage(region.position, region.extents);
        }
        windowSurface.commit();
      });

      Window window{context};
      co_await when_any(receiver(coro_just(window)), std::move(redrawOnChange));
    }

    auto subscribe(std::function<auto(IoTask<Window>)->IoTask<void>> receiver) && noexcept
        -> IoTask<void> {
      return do_subscribe(std::move(mRootWidget), std::move(receiver));
    }

    std::unique_ptr<Widget> mRootWidget;
  };
  return WindowObservable{std::move(rootWidget)};
}

} // namespace cw