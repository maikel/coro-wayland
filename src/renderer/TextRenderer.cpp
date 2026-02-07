// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "TextRenderer.hpp"
#include "Font.hpp"
#include "GlyphCache.hpp"
#include "narrow.hpp"

#include <algorithm>

namespace cw {

struct TextRendererImpl {
  GlyphCache* cache;

  explicit TextRendererImpl(GlyphCache& cache) : cache(&cache) {}

  // Blend a single pixel with alpha
  static auto blend_pixel(std::uint8_t src_alpha, Color color, std::uint32_t& dest_pixel) -> void {
    if (src_alpha == 0)
      return;

    // Extract ARGB components from dest (assuming ARGB32 format)
    std::uint8_t dest_a = (dest_pixel >> 24) & 0xFF;
    std::uint8_t dest_r = (dest_pixel >> 16) & 0xFF;
    std::uint8_t dest_g = (dest_pixel >> 8) & 0xFF;
    std::uint8_t dest_b = dest_pixel & 0xFF;

    // Alpha blend: out = src * src_alpha + dest * (1 - src_alpha)
    float alpha = src_alpha / 255.0f * color.a / 255.0f;
    float inv_alpha = 1.0f - alpha;

    std::uint8_t out_r = static_cast<std::uint8_t>(color.r * alpha + dest_r * inv_alpha);
    std::uint8_t out_g = static_cast<std::uint8_t>(color.g * alpha + dest_g * inv_alpha);
    std::uint8_t out_b = static_cast<std::uint8_t>(color.b * alpha + dest_b * inv_alpha);
    std::uint8_t out_a = std::max(dest_a, static_cast<std::uint8_t>(alpha * 255));

    dest_pixel = (static_cast<std::uint32_t>(out_a) << 24) |
                 (static_cast<std::uint32_t>(out_r) << 16) |
                 (static_cast<std::uint32_t>(out_g) << 8) | static_cast<std::uint32_t>(out_b);
  }

  auto draw_glyph(PixelsView pixels, CachedGlyph const& glyph, std::int32_t x, std::int32_t y,
                  Color color) -> void {
    std::int32_t glyph_x = x + glyph.metrics.bearing_x;
    std::int32_t glyph_y = y - glyph.metrics.bearing_y;

    std::size_t buffer_height = pixels.height();
    std::size_t buffer_width = pixels.width();

    for (std::uint32_t row = 0; row < glyph.metrics.height; ++row) {
      auto target_y = narrow<std::size_t>(glyph_y + static_cast<std::int32_t>(row));
      if (target_y < 0 || static_cast<std::size_t>(target_y) >= buffer_height)
        continue;

      for (std::uint32_t col = 0; col < glyph.metrics.width; ++col) {
        auto target_x = narrow<std::size_t>(glyph_x + static_cast<std::int32_t>(col));
        if (target_x < 0 || static_cast<std::size_t>(target_x) >= buffer_width)
          continue;

        std::uint8_t alpha = glyph.bitmap[row * glyph.metrics.width + col];
        blend_pixel(alpha, color, pixels[target_x, target_y]);
      }
    }
  }
};

TextRenderer::TextRenderer(GlyphCache& cache) : mImpl(std::make_unique<TextRendererImpl>(cache)) {}

TextRenderer::TextRenderer(TextRenderer&&) noexcept = default;
auto TextRenderer::operator=(TextRenderer&&) noexcept -> TextRenderer& = default;
TextRenderer::~TextRenderer() = default;

auto TextRenderer::draw_text(PixelsView pixels, Font const& font, std::string_view text,
                             Color color) -> void {
  if (!font.is_valid())
    return;

  // Calculate baseline position: use the font's ascent as the baseline y
  FontMetrics font_metrics = font.metrics();
  std::int32_t baseline_y = font_metrics.ascent;

  std::int32_t cursor_x = 0;
  std::uint32_t prev_glyph = 0;

  for (char c : text) {
    // Convert ASCII to glyph index
    std::uint32_t glyph_index = font.get_glyph_index(static_cast<char32_t>(c));
    if (glyph_index == 0)
      continue; // Skip missing glyphs

    // Apply kerning
    if (prev_glyph != 0) {
      cursor_x += font.get_kerning(prev_glyph, glyph_index);
    }

    // Get glyph from cache
    CachedGlyph glyph = mImpl->cache->get(font, glyph_index);

    // Draw glyph
    mImpl->draw_glyph(pixels, glyph, cursor_x, baseline_y, color);

    // Advance cursor
    cursor_x += glyph.metrics.advance_x;
    prev_glyph = glyph_index;
  }
}

auto TextRenderer::measure_text(Font const& font, std::string_view text) const -> Extents {
  if (!font.is_valid()) {
    return {};
  }

  FontMetrics font_metrics = font.metrics();
  std::int32_t width = 0;
  std::uint32_t prev_glyph = 0;

  for (char c : text) {
    std::uint32_t glyph_index = font.get_glyph_index(static_cast<char32_t>(c));
    if (glyph_index == 0)
      continue;

    if (prev_glyph != 0) {
      width += font.get_kerning(prev_glyph, glyph_index);
    }

    GlyphMetrics glyph_metrics = font.get_glyph_metrics(glyph_index);
    width += glyph_metrics.advance_x;
    prev_glyph = glyph_index;
  }

  return Extents{width, font_metrics.line_height};
}

} // namespace cw
