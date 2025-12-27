#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <mdspan>
#include <vector>

namespace graphics {

/**
 * Basic 2D point structure
 */
struct Point {
  int x, y;

  Point() = default;
  Point(int x, int y) : x(x), y(y) {}

  Point operator+(const Point& other) const { return {x + other.x, y + other.y}; }

  Point operator-(const Point& other) const { return {x - other.x, y - other.y}; }

  bool operator==(const Point& other) const = default;
};

/**
 * RGBA color structure
 */
struct Color {
  std::uint8_t r, g, b, a;

  constexpr Color() : r(0), g(0), b(0), a(255) {}
  constexpr Color(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
      : r(r), g(g), b(b), a(a) {}

  // Convert to 32-bit RGBA
  constexpr std::uint32_t to_rgba() const {
    return (static_cast<std::uint32_t>(r) << 24) | (static_cast<std::uint32_t>(g) << 16) |
           (static_cast<std::uint32_t>(b) << 8) | static_cast<std::uint32_t>(a);
  }

  // Create from 32-bit RGBA
  static constexpr Color from_rgba(std::uint32_t rgba) {
    return Color{static_cast<std::uint8_t>((rgba >> 24) & 0xFF),
                 static_cast<std::uint8_t>((rgba >> 16) & 0xFF),
                 static_cast<std::uint8_t>((rgba >> 8) & 0xFF),
                 static_cast<std::uint8_t>(rgba & 0xFF)};
  }

  bool operator==(const Color& other) const = default;
};

// Predefined colors
namespace colors {
inline constexpr Color BLACK{0, 0, 0};
inline constexpr Color WHITE{255, 255, 255};
inline constexpr Color RED{255, 0, 0};
inline constexpr Color GREEN{0, 255, 0};
inline constexpr Color BLUE{0, 0, 255};
inline constexpr Color YELLOW{255, 255, 0};
inline constexpr Color CYAN{0, 255, 255};
inline constexpr Color MAGENTA{255, 0, 255};
} // namespace colors

/**
 * Concept for pixel buffer types
 */
template <typename T>
concept PixelBuffer = requires(T buffer, std::size_t x, std::size_t y, Color color) {
  { buffer.extent(0) } -> std::convertible_to<std::size_t>;
  { buffer.extent(1) } -> std::convertible_to<std::size_t>;
  { buffer[x, y] } -> std::assignable_from<std::uint32_t>;
};

/**
 * Convenience type alias for 2D mdspan over uint32_t pixels
 */
using PixelSpan = std::mdspan<std::uint32_t, std::dextents<std::size_t, 2>>;
using ConstPixelSpan = std::mdspan<const std::uint32_t, std::dextents<std::size_t, 2>>;

/**
 * Simple pixel buffer class wrapping a std::vector and providing mdspan access
 */
class PixelBufferOwned {
private:
  std::vector<std::uint32_t> data_;
  std::size_t width_, height_;

public:
  PixelBufferOwned(std::size_t width, std::size_t height)
      : data_(width * height, colors::BLACK.to_rgba()), width_(width), height_(height) {}

  PixelSpan span() {
    return PixelSpan{data_.data(), std::dextents<std::size_t, 2>{width_, height_}};
  }

  ConstPixelSpan span() const {
    return ConstPixelSpan{data_.data(), std::dextents<std::size_t, 2>{width_, height_}};
  }

  std::size_t width() const { return width_; }
  std::size_t height() const { return height_; }

  // Direct access to underlying data for serialization/display
  const std::uint32_t* data() const { return data_.data(); }
  std::uint32_t* data() { return data_.data(); }

  void clear(Color color = colors::BLACK) {
    std::fill(data_.begin(), data_.end(), color.to_rgba());
  }
};

} // namespace graphics
