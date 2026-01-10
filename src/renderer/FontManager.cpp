// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "FontManager.hpp"
#include "Font.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <filesystem>
#include <vector>

namespace cw {

// Forward declare FontImpl from Font.cpp
struct FontImpl;
struct FontImplDeleter;
extern auto make_font_impl(FT_Face face) -> std::unique_ptr<FontImpl, FontImplDeleter>;

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
        result += std::tolower(static_cast<unsigned char>(c));
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

} // namespace cw