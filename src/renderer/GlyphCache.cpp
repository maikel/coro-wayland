// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "GlyphCache.hpp"
#include "Font.hpp"

#include <unordered_map>
#include <vector>

namespace cw {

namespace {
struct CacheKey {
  Font const* font;
  std::uint32_t glyph_index;

  auto operator==(CacheKey const& other) const -> bool {
    return font == other.font && glyph_index == other.glyph_index;
  }
};

struct CacheKeyHash {
  auto operator()(CacheKey const& key) const -> std::size_t {
    return std::hash<void const*>{}(key.font) ^ (std::hash<std::uint32_t>{}(key.glyph_index) << 1);
  }
};

struct CachedGlyphData {
  std::vector<std::uint8_t> bitmap;
  GlyphMetrics metrics;
};
} // namespace

struct GlyphCacheImpl {
  std::unordered_map<CacheKey, CachedGlyphData, CacheKeyHash> cache;
  std::size_t total_bytes = 0;
};

GlyphCache::GlyphCache() : mImpl(std::make_unique<GlyphCacheImpl>()) {}

GlyphCache::GlyphCache(GlyphCache&&) noexcept = default;
auto GlyphCache::operator=(GlyphCache&&) noexcept -> GlyphCache& = default;
GlyphCache::~GlyphCache() = default;

auto GlyphCache::get(Font const& font, std::uint32_t glyph_index) -> CachedGlyph {
  CacheKey key{&font, glyph_index};

  // Check if already cached
  auto it = mImpl->cache.find(key);
  if (it != mImpl->cache.end()) {
    return CachedGlyph{.bitmap = it->second.bitmap, .metrics = it->second.metrics};
  }

  // Load and cache the glyph
  GlyphMetrics metrics = font.get_glyph_metrics(glyph_index);
  std::uint8_t const* bitmap_data = font.load_glyph_bitmap(glyph_index);

  // Handle glyphs with no bitmap (like spaces) - still need metrics for advance
  if (!bitmap_data || metrics.width == 0 || metrics.height == 0) {
    // Cache the glyph with empty bitmap but preserve metrics
    auto [inserted_it, success] =
        mImpl->cache.emplace(key, CachedGlyphData{std::vector<std::uint8_t>{}, metrics});
    return CachedGlyph{.bitmap = inserted_it->second.bitmap,
                       .metrics = inserted_it->second.metrics};
  }

  // Copy bitmap data into cache
  std::size_t bitmap_size = metrics.width * metrics.height;
  std::vector<std::uint8_t> bitmap_copy(bitmap_data, bitmap_data + bitmap_size);

  mImpl->total_bytes += bitmap_size;

  auto [inserted_it, success] =
      mImpl->cache.emplace(key, CachedGlyphData{std::move(bitmap_copy), metrics});

  return CachedGlyph{.bitmap = inserted_it->second.bitmap, .metrics = inserted_it->second.metrics};
}

auto GlyphCache::clear() -> void {
  mImpl->cache.clear();
  mImpl->total_bytes = 0;
}

auto GlyphCache::size() const -> std::size_t { return mImpl->cache.size(); }

auto GlyphCache::memory_usage() const -> std::size_t { return mImpl->total_bytes; }

} // namespace cw
