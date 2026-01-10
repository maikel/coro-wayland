// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Font.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

namespace cw {

struct FontImpl {
  FT_Face face = nullptr;
};

void FontImplDeleter::operator()(FontImpl* ptr) const {
  if (ptr && ptr->face) {
    FT_Done_Face(ptr->face);
  }
  delete ptr;
}

auto make_font_impl(FT_Face face) -> std::unique_ptr<FontImpl, FontImplDeleter> {
  return std::unique_ptr<FontImpl, FontImplDeleter>(new FontImpl{face});
}

Font::Font(std::unique_ptr<FontImpl, FontImplDeleter> impl) : mImpl(std::move(impl)) {}

Font::Font(Font&&) noexcept = default;
auto Font::operator=(Font&&) noexcept -> Font& = default;
Font::~Font() = default;

auto Font::metrics() const -> FontMetrics {
  if (!mImpl || !mImpl->face) {
    return {};
  }

  FT_Face face = mImpl->face;
  return FontMetrics{.ascent = static_cast<std::int32_t>(face->size->metrics.ascender >> 6),
                     .descent = static_cast<std::int32_t>(face->size->metrics.descender >> 6),
                     .line_height = static_cast<std::int32_t>(face->size->metrics.height >> 6),
                     .size_px = static_cast<std::uint32_t>(face->size->metrics.y_ppem)};
}

auto Font::get_glyph_index(char32_t codepoint) const -> std::uint32_t {
  if (!mImpl || !mImpl->face) {
    return 0;
  }

  return FT_Get_Char_Index(mImpl->face, codepoint);
}

auto Font::get_glyph_metrics(std::uint32_t glyph_index) const -> GlyphMetrics {
  if (!mImpl || !mImpl->face) {
    return {};
  }

  FT_Face face = mImpl->face;
  FT_Error error = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
  if (error) {
    return {};
  }

  return GlyphMetrics{
      .width = static_cast<std::uint32_t>(face->glyph->metrics.width >> 6),
      .height = static_cast<std::uint32_t>(face->glyph->metrics.height >> 6),
      .bearing_x = static_cast<std::int32_t>(face->glyph->metrics.horiBearingX >> 6),
      .bearing_y = static_cast<std::int32_t>(face->glyph->metrics.horiBearingY >> 6),
      .advance_x = static_cast<std::int32_t>(face->glyph->metrics.horiAdvance >> 6)};
}

auto Font::load_glyph_bitmap(std::uint32_t glyph_index) const -> std::uint8_t const* {
  if (!mImpl || !mImpl->face) {
    return nullptr;
  }

  FT_Face face = mImpl->face;
  FT_Error error = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
  if (error) {
    return nullptr;
  }

  error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
  if (error) {
    return nullptr;
  }

  return face->glyph->bitmap.buffer;
}

auto Font::get_kerning(std::uint32_t left_glyph, std::uint32_t right_glyph) const -> std::int32_t {
  if (!mImpl || !mImpl->face) {
    return 0;
  }

  FT_Face face = mImpl->face;
  if (!FT_HAS_KERNING(face)) {
    return 0;
  }

  FT_Vector delta;
  FT_Error error = FT_Get_Kerning(face, left_glyph, right_glyph, FT_KERNING_DEFAULT, &delta);
  if (error) {
    return 0;
  }

  return static_cast<std::int32_t>(delta.x >> 6);
}

auto Font::is_valid() const -> bool { return mImpl && mImpl->face; }

} // namespace cw
