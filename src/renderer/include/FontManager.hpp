// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cw {

struct FontManagerError : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

class Font;

// Manages font discovery, loading, and caching
class FontManager {
public:
  FontManager();
  FontManager(FontManager&&) noexcept;
  auto operator=(FontManager&&) noexcept -> FontManager&;
  ~FontManager();

  // Load a font by family name and size in pixels
  // Throws FontManagerError if font cannot be loaded
  auto load_font(std::string_view family, std::uint32_t size_px) -> Font;

  // Load a font from a specific file path
  auto load_font_file(std::string_view path, std::uint32_t size_px) -> Font;

  // Add a directory to search for fonts
  auto add_font_directory(std::string_view path) -> void;

private:
  std::unique_ptr<struct FontManagerImpl> mImpl;
};

} // namespace cw