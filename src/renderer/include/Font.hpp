// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace cw {

struct FontImpl;
struct FontImplDeleter {
  void operator()(FontImpl* ptr) const;
};

struct FontMetrics {
  std::int32_t ascent;      // Distance from baseline to highest point
  std::int32_t descent;     // Distance from baseline to lowest point
  std::int32_t line_height; // Recommended line spacing
  std::uint32_t size_px;    // Font size in pixels
};

struct GlyphMetrics {
  std::uint32_t width;    // Glyph bitmap width
  std::uint32_t height;   // Glyph bitmap height
  std::int32_t bearing_x; // Horizontal bearing (left side)
  std::int32_t bearing_y; // Vertical bearing (top side)
  std::int32_t advance_x; // Horizontal advance to next glyph
};

// Represents a loaded font at a specific size
// Does not handle rendering - only provides metrics and glyph data
class Font {
public:
  Font(Font&&) noexcept;
  auto operator=(Font&&) noexcept -> Font&;
  ~Font();

  // Get overall font metrics
  auto metrics() const -> FontMetrics;

  // Get glyph index for a character (or 0 for missing glyph)
  auto get_glyph_index(char32_t codepoint) const -> std::uint32_t;

  // Get metrics for a specific glyph
  auto get_glyph_metrics(std::uint32_t glyph_index) const -> GlyphMetrics;

  // Load glyph bitmap (grayscale 8-bit)
  // Returns pointer to internal buffer, valid until next load_glyph call
  // Returns nullptr if glyph cannot be loaded
  auto load_glyph_bitmap(std::uint32_t glyph_index) const -> std::uint8_t const*;

  // Get kerning adjustment between two glyphs (in pixels)
  auto get_kerning(std::uint32_t left_glyph, std::uint32_t right_glyph) const -> std::int32_t;

  // Check if font is valid (not moved-from)
  auto is_valid() const -> bool;

private:
  explicit Font(std::unique_ptr<FontImpl, FontImplDeleter> impl);
  friend class FontManager;
  std::unique_ptr<FontImpl, FontImplDeleter> mImpl;
};

} // namespace cw