// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "PixelsView.hpp"
#include "TextRenderer.hpp"

#include <cstdint>
#include <mdspan>
#include <string_view>

namespace cw {

// Provides rendering primitives and manages rendering state
class RenderContext {
public:
  RenderContext(PixelsView buffer, TextRenderer& text_renderer);

  // Measure text dimensions without rendering
  auto measure_text(Font const& font, std::string_view text) const -> Extents;

  // Draw text at absolute position
  auto draw_text(Font const& font, std::string_view text, Position position, Color color) -> void;

  // Fill rectangle with solid color
  auto fill_rect(Region region, Color color) -> void;

  // Get buffer dimensions
  auto buffer_size() const -> Extents;

private:
  PixelsView mPixels;
  TextRenderer* mTextRenderer;
};

} // namespace cw
