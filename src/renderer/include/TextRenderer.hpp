// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "PixelsView.hpp"

namespace cw {

class Font;
class GlyphCache;

struct Color {
  static auto from_argb(std::uint32_t argb) -> Color {
    return Color{.r = static_cast<std::uint8_t>((argb >> 16) & 0xFF),
                 .g = static_cast<std::uint8_t>((argb >> 8) & 0xFF),
                 .b = static_cast<std::uint8_t>(argb & 0xFF),
                 .a = static_cast<std::uint8_t>((argb >> 24) & 0xFF)};
  }

  auto to_argb() const -> std::uint32_t {
    return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16) |
           (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
  }

  auto to_rgba() const -> std::uint32_t {
    return (static_cast<std::uint32_t>(r) << 24) | (static_cast<std::uint32_t>(g) << 16) |
           (static_cast<std::uint32_t>(b) << 8) | static_cast<std::uint32_t>(a);
  }

  std::uint8_t r, g, b, a;
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
  auto draw_text(PixelsView buffer, Font const& font, std::string_view text, Color color) -> void;

  // Calculate text metrics without rendering
  auto measure_text(Font const& font, std::string_view text) const -> Extents;

private:
  std::unique_ptr<struct TextRendererImpl> mImpl;
};

} // namespace cw