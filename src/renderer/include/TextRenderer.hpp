// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <cstdint>
#include <mdspan>
#include <memory>
#include <string_view>

namespace cw {

class Font;
class GlyphCache;

struct Color {
  std::uint8_t r, g, b, a;
};

struct TextMetrics {
  std::int32_t width;  // Total width of rendered text
  std::int32_t height; // Total height (based on font metrics)
};

// Renders text onto a buffer using fonts and glyph cache
// Buffer format: ARGB32
class TextRenderer {
public:
  explicit TextRenderer(GlyphCache& cache);
  TextRenderer(TextRenderer&&) noexcept;
  auto operator=(TextRenderer&&) noexcept -> TextRenderer&;
  ~TextRenderer();

  // Render ASCII text onto buffer at given position
  // buffer: 2D view of ARGB32 pixels [height, width]
  auto draw_text(std::mdspan<std::uint32_t, std::dextents<std::size_t, 2>> buffer, Font const& font,
                 std::string_view text, std::int32_t x, std::int32_t y, Color color) -> void;

  // Calculate text metrics without rendering
  auto measure_text(Font const& font, std::string_view text) const -> TextMetrics;

private:
  std::unique_ptr<struct TextRendererImpl> mImpl;
};

} // namespace cw