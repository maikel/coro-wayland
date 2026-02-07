// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "RenderContext.hpp"

namespace cw {

RenderContext::RenderContext(PixelsView buffer, TextRenderer& text_renderer)
    : mPixels(std::move(buffer)), mTextRenderer(&text_renderer) {}

// Measure text dimensions without rendering
auto RenderContext::measure_text(Font const& font, std::string_view text) const -> Extents {
  return mTextRenderer->measure_text(font, text);
}

// Draw text at absolute position
auto RenderContext::draw_text(Font const& font, std::string_view text, Position position, Color color)
    -> void {
  mTextRenderer->draw_text(this->mPixels.subview(position), font, text, color);
}

// Fill rectangle with solid color
auto RenderContext::fill_rect(Region region, Color color) -> void {
  for (std::size_t y = 0; y < region.size.extent(1); ++y) {
    for (std::size_t x = 0; x < region.size.extent(0); ++x) {
      mPixels[region.position.x + x, region.position.y + y] = color.to_argb();
    }
  }
}

// Get buffer dimensions
auto RenderContext::buffer_size() const -> Extents { return mPixels.extents(); }

} // namespace cw