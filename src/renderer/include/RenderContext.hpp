// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "BoxConstraints.hpp"
#include "PixelsView.hpp"

#include <cstdint>
#include <mdspan>
#include <string_view>

namespace cw {

class Font;
class GlyphCache;
class TextRenderer;

struct Color;

// Provides rendering primitives and manages rendering state
class RenderContext {
public:
  RenderContext(PixelsView buffer, TextRenderer& text_renderer);

  // Draw text at absolute position
  auto draw_text(Font const& font, std::string_view text, Offset position, Color color) -> void;

  // Fill rectangle with solid color
  auto fill_rect(Rect rect, Color color) -> void;

  // Get buffer dimensions
  auto buffer_size() const -> Size;

private:
  PixelsView mPixels;
  TextRenderer* mTextRenderer;
};

} // namespace cw
