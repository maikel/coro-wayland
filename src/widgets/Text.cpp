// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Text.hpp"
//
#include "AsyncChannel.hpp"
#include "TextRenderer.hpp"
#include "observables/single.hpp"

namespace cw {
struct TextRenderContext {
  AsyncChannel<void> mRedraw;
  TextRenderer renderContext;
};

struct TextRenderObject : RenderObject {
  TextRenderContext* mContext;
};

Text::Text(Observable<TextProperties>&& properties) : mProperties(std::move(properties)) {}

Text::Text(Font const& font, std::string text, std::uint32_t color)
    : mProperties(observables::single(coro_just(TextProperties{text, color, font}))) {}

auto Text::render_object() && -> Observable<AnyRenderObject> {}

} // namespace cw