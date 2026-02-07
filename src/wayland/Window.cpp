// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "wayland/Window.hpp"

#include "narrow.hpp"
#include "when_all.hpp"
#include "when_any.hpp"

#include "Logging.hpp"

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

auto Window::make(AnyWidget rootWidget) -> Observable<Window> {
  struct WindowObservable {
    static auto do_subscribe(AnyWidget rootWidget,
                             std::function<auto(IoTask<Window>)->IoTask<void>> receiver)
        -> IoTask<void> {
      Client client = co_await use_resource(Client::make());
      FrameBufferPool frameBufferPool = co_await use_resource(FrameBufferPool::make(client));
      WindowSurface windowSurface = co_await use_resource(WindowSurface::make(client));
      WindowContext context{client, frameBufferPool, windowSurface};
      GlyphCache glyphCache{};
      TextRenderer textRenderer(glyphCache);
      AnyRenderObject rootRenderObject =
          co_await use_resource(std::move(rootWidget).render_object());
      Size configuredBounds{};

      auto redrawOnChange = rootRenderObject->dirty().subscribe([&](auto isDirty) -> IoTask<void> {
        co_await when_all(std::move(isDirty), windowSurface.frame());
        // Log::i("Redrawing window due to dirty render object");
        // auto available = co_await frameBufferPool.available_buffer();
        // RenderContext renderContext{available.pixels, textRenderer};
        // auto regions = rootRenderObject->render(renderContext);
        // windowSurface.attach(available.buffer);
        // for (const auto& region : regions) {
        //   windowSurface.damage(region);
        // }
        // windowSurface.commit();
      });

      auto configureBounds =
          windowSurface.configure_bounds_events().subscribe([&](auto eventTask) -> IoTask<void> {
            auto event = co_await std::move(eventTask);
            configuredBounds = Size{.width = narrow<std::size_t>(event.width),
                                    .height = narrow<std::size_t>(event.height)};
          });

      auto configureFrameBuffer =
          windowSurface.configure_events().subscribe([&](auto eventTask) -> IoTask<void> {
            auto event = co_await std::move(eventTask);
            auto available = co_await frameBufferPool.available_buffer();
            BoxConstraints constraints = BoxConstraints::loose(configuredBounds);
            RenderContext fullContext{available.pixels, textRenderer};
            BoxConstraints newConstraints = rootRenderObject->layout(fullContext, constraints);
            co_await frameBufferPool.resize(Width{newConstraints.smallest().width},
                                            Height{newConstraints.smallest().height});
            available = co_await frameBufferPool.available_buffer();
            PixelsView pixels =
                available.pixels.subview(Position{0, 0}, Extents{newConstraints.smallest().width,
                                                                 newConstraints.smallest().height});
            RenderContext renderContext{pixels, textRenderer};
            auto regions = rootRenderObject->render(renderContext);
            windowSurface.attach(available.buffer);
            for (const auto& region : regions) {
              windowSurface.damage(region);
            }
            windowSurface.commit();
          });

      Window window{context};
      co_await when_any(receiver(coro_just(window)), std::move(redrawOnChange),
                        std::move(configureBounds), std::move(configureFrameBuffer));
    }

    auto subscribe(std::function<auto(IoTask<Window>)->IoTask<void>> receiver) && noexcept
        -> IoTask<void> {
      return do_subscribe(std::move(mRootWidget), std::move(receiver));
    }

    AnyWidget mRootWidget;
  };
  return WindowObservable{std::move(rootWidget)};
}

} // namespace cw