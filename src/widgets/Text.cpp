// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Text.hpp"
//
#include "AsyncChannel.hpp"
#include "AsyncQueue.hpp"
#include "Font.hpp"
#include "GlyphCache.hpp"
#include "RenderContext.hpp"
#include "TextRenderer.hpp"
#include "Widget.hpp"
#include "coro_just.hpp"
#include "observables/first.hpp"
#include "observables/single.hpp"
#include "observables/then.hpp"
#include "observables/use_resource.hpp"

namespace cw {
struct TextRenderContext {
  AsyncChannel<void> mRedraw;
  TextProperties mProperties;
  std::uint64_t mRevision{0};
  bool mDirty{true};
};

struct TextRenderObject final : RenderObject {
  TextRenderContext* mContext;

  explicit TextRenderObject(TextRenderContext* context) : mContext(context) {}

  ~TextRenderObject() = default;

  auto layout(const RenderContext& context, BoxConstraints constraints) -> BoxConstraints override {
    // For simplicity, assume text fits within constraints
    auto extents = context.measure_text(mContext->mProperties.font, mContext->mProperties.text);
    return BoxConstraints::tight(
        {std::clamp(extents.extent(0), constraints.min_width, constraints.max_width),
         std::clamp(extents.extent(1), constraints.min_height, constraints.max_height)});
  }

  auto render(RenderContext& context, bool redraw) -> std::vector<Region> override {
    if (redraw || mContext->mDirty) {
      mContext->mDirty = false;
    } else {
      return {};
    }
    Position offset{.x = 0, .y = 0};
    context.draw_text(mContext->mProperties.font, mContext->mProperties.text, offset,
                      Color::from_argb(mContext->mProperties.color));
    return {Region{{0, 0}, context.buffer_size()}};
  }

  auto dirty() const -> Observable<void> override { return mContext->mRedraw.receive(); }
};

Text::Text(Observable<TextProperties>&& properties) : mProperties(std::move(properties)) {}

Text::Text(Font const& font, std::string text, std::uint32_t color)
    : mProperties(observables::single(coro_just(TextProperties{text, color, font}))) {}

auto Text::render_object() && -> Observable<AnyRenderObject> {
  struct TextObservable {
    Observable<TextProperties> mProperties;

    static auto do_subscribe(Observable<TextProperties> properties,
                             std::function<auto(IoTask<AnyRenderObject>)->IoTask<void>> receiver)
        -> IoTask<void> {
      AsyncChannel<TextProperties> propertiesChannel =
          co_await use_resource(AsyncChannel<TextProperties>::make());
      AsyncQueue<std::uint64_t> revisionQueue =
          co_await use_resource(AsyncQueue<std::uint64_t>::make());
      AsyncChannel<void> redrawChannel = co_await use_resource(AsyncChannel<void>::make());
      StoppableScope scope = co_await use_resource(StoppableScope::make());
      scope.spawn(
          std::move(properties).subscribe([&](IoTask<TextProperties> propertyTask) -> IoTask<void> {
            TextProperties properties = co_await std::move(propertyTask);
            co_await propertiesChannel.send(std::move(properties));
          }));
      TextProperties initialProperties = co_await observables::first(propertiesChannel.receive());
      TextRenderContext context{redrawChannel, initialProperties};

      co_await
          [](TextRenderContext* context, AsyncChannel<TextProperties> propertiesChannel,
             AsyncQueue<std::uint64_t> revisionQueue,
             std::function<auto(IoTask<AnyRenderObject>)->IoTask<void>> receiver) -> IoTask<void> {
            StoppableScope scope = co_await use_resource(StoppableScope::make());
            scope.spawn(propertiesChannel.receive().subscribe(
                [&](IoTask<TextProperties> propertyTask) -> IoTask<void> {
                  TextProperties properties = co_await std::move(propertyTask);
                  context->mProperties = std::move(properties);
                  context->mRevision += 1;
                  context->mDirty = true;
                  co_await revisionQueue.push(context->mRevision);
                }));
            scope.spawn(revisionQueue.observable().subscribe(
                [&](IoTask<std::uint64_t> revisionTask) -> IoTask<void> {
                  while (true) {
                    std::uint64_t revision = co_await std::move(revisionTask);
                    if (revision == context->mRevision) {
                      co_await context->mRedraw.send();
                      break;
                    }
                  }
                }));
            auto renderObject = coro_just(AnyRenderObject{TextRenderObject{context}});
            co_await receiver(std::move(renderObject));
          }(&context, propertiesChannel, revisionQueue, std::move(receiver));
    }

    auto subscribe(std::function<auto(IoTask<AnyRenderObject>)->IoTask<void>> receiver) && noexcept
        -> IoTask<void> {
      return do_subscribe(std::move(mProperties), std::move(receiver));
    }
  };
  return TextObservable{std::move(mProperties)};
}

} // namespace cw