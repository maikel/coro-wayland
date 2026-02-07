// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Font.hpp"

#include <filesystem>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace cw {

struct FontImpl {
  FT_Face face = nullptr;
};

struct FontImplDeleter {
  void operator()(FontImpl* ptr) const;
};

void FontImplDeleter::operator()(FontImpl* ptr) const {
  if (ptr && ptr->face) {
    FT_Done_Face(ptr->face);
  }
  delete ptr;
}

auto make_font_impl(FT_Face face) -> std::shared_ptr<FontImpl> {
  return std::shared_ptr<FontImpl>(new FontImpl{face}, FontImplDeleter{});
}

Font::Font(std::shared_ptr<FontImpl> impl) : mImpl(std::move(impl)) {}

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

struct FontManagerImpl {
  FT_Library library = nullptr;
  std::vector<std::filesystem::path> font_directories;

  FontManagerImpl() {
    FT_Error error = FT_Init_FreeType(&library);
    if (error) {
      throw FontManagerError("Failed to initialize FreeType library");
    }

    // Add default system font directories
    font_directories.push_back("/usr/share/fonts");
    font_directories.push_back("/usr/local/share/fonts");

    // Add user font directory if it exists
    if (auto home = std::getenv("HOME")) {
      std::filesystem::path user_fonts = std::filesystem::path(home) / ".fonts";
      if (std::filesystem::exists(user_fonts)) {
        font_directories.push_back(user_fonts);
      }
    }
  }

  ~FontManagerImpl() {
    if (library) {
      FT_Done_FreeType(library);
    }
  }

  static auto normalize_font_name(std::string_view name) -> std::string {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
      if (c != ' ' && c != '-') {
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
    }
    return result;
  }

  auto find_font_file(std::string_view family) -> std::filesystem::path {
    // Normalize search term by removing spaces and converting to lowercase
    std::string normalized_family = normalize_font_name(family);

    for (auto const& dir : font_directories) {
      if (!std::filesystem::exists(dir))
        continue;

      for (auto const& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file())
          continue;

        auto ext = entry.path().extension();
        if (ext != ".ttf" && ext != ".otf" && ext != ".TTF" && ext != ".OTF") {
          continue;
        }

        // Normalize filename and check if it contains the family name
        auto filename = entry.path().stem().string(); // stem removes extension
        std::string normalized_filename = normalize_font_name(filename);

        if (normalized_filename.find(normalized_family) != std::string::npos) {
          return entry.path();
        }
      }
    }

    throw FontManagerError("Font family not found: " + std::string(family));
  }
};

FontManager::FontManager() : mImpl(std::make_unique<FontManagerImpl>()) {}

FontManager::FontManager(FontManager&&) noexcept = default;
auto FontManager::operator=(FontManager&&) noexcept -> FontManager& = default;
FontManager::~FontManager() = default;

auto FontManager::load_font(std::string_view family, std::uint32_t size_px) -> Font {
  auto path = mImpl->find_font_file(family);
  return load_font_file(path.string(), size_px);
}

auto FontManager::load_font_file(std::string_view path, std::uint32_t size_px) -> Font {
  FT_Face face;
  FT_Error error = FT_New_Face(mImpl->library, path.data(), 0, &face);
  if (error) {
    throw FontManagerError("Failed to load font file: " + std::string(path));
  }

  // Set pixel size
  error = FT_Set_Pixel_Sizes(face, 0, size_px);
  if (error) {
    FT_Done_Face(face);
    throw FontManagerError("Failed to set font size");
  }

  return Font(make_font_impl(face));
}

auto FontManager::add_font_directory(std::string_view path) -> void {
  mImpl->font_directories.push_back(path);
}

auto FontManager::get_default() -> Font { return load_font("Dejavu Sans Mono", 12); }

} // namespace cw
