// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "Font.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace cw {

struct CachedGlyph {
  std::span<std::uint8_t const> bitmap; // Grayscale bitmap data
  GlyphMetrics metrics;
};

// Caches rasterized glyph bitmaps to avoid re-rendering
class GlyphCache {
public:
  GlyphCache();
  GlyphCache(GlyphCache&&) noexcept;
  auto operator=(GlyphCache&&) noexcept -> GlyphCache&;
  ~GlyphCache();

  // Get or load a glyph from cache
  // Returns cached glyph with bitmap and metrics
  auto get(Font const& font, std::uint32_t glyph_index) -> CachedGlyph;

  // Clear all cached glyphs
  auto clear() -> void;

  // Get cache statistics
  auto size() const -> std::size_t;
  auto memory_usage() const -> std::size_t;

private:
  std::unique_ptr<struct GlyphCacheImpl> mImpl;
};

} // namespace cw