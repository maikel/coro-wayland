// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <algorithm>
#include <cstddef>

namespace cw {

struct Offset {
  std::int32_t x = 0;
  std::int32_t y = 0;

  auto operator==(Offset const&) const -> bool = default;
};

struct Size {
  std::size_t width = 0;
  std::size_t height = 0;

  auto operator==(Size const&) const -> bool = default;
};

struct Rect {
  Offset offset;
  Size size;

  auto operator==(Rect const&) const -> bool = default;
};

// Immutable layout constraints passed from parent to child
struct BoxConstraints {
  std::size_t min_width = 0;
  std::size_t max_width = 0;
  std::size_t min_height = 0;
  std::size_t max_height = 0;

  // Create tight constraints (child must be exact size)
  static auto tight(Size size) -> BoxConstraints {
    return BoxConstraints{size.width, size.width, size.height, size.height};
  }

  // Create loose constraints (child can be any size up to max)
  static auto loose(Size size) -> BoxConstraints {
    return BoxConstraints{0, size.width, 0, size.height};
  }

  // Create constraints with specific bounds
  static auto bounded(std::size_t max_width, std::size_t max_height) -> BoxConstraints {
    return BoxConstraints{0, max_width, 0, max_height};
  }

  // Check if constraints require exact size
  auto is_tight() const -> bool { return min_width == max_width && min_height == max_height; }

  // Get the biggest size that satisfies constraints
  auto biggest() const -> Size { return {max_width, max_height}; }

  // Get the smallest size that satisfies constraints
  auto smallest() const -> Size { return {min_width, min_height}; }

  // Constrain a size to fit within these constraints
  auto constrain(Size size) const -> Size {
    return {std::clamp(size.width, min_width, max_width),
            std::clamp(size.height, min_height, max_height)};
  }

  auto operator==(BoxConstraints const&) const -> bool = default;
};

} // namespace cw
