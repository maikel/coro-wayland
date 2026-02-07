// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <mdspan>

namespace cw {

struct Position {
  std::size_t x;
  std::size_t y;

  auto operator==(Position const&) const -> bool = default;
};

using Extents = std::dextents<std::size_t, 2>;


struct Region {
  Position position;
  Extents size;

  auto operator==(Region const&) const -> bool = default;
};

class PixelsView {
public:
  PixelsView() = default;

  explicit PixelsView(std::span<std::uint32_t> data, Extents extents) noexcept;

  explicit PixelsView(std::mdspan<std::uint32_t, std::dextents<std::size_t, 2>, std::layout_stride>
                          pixels) noexcept;

  auto width() const -> std::size_t;
  auto height() const -> std::size_t;

  auto data() const -> std::uint32_t*;

  auto extents() const -> Extents;

  auto row_stride() const -> std::size_t;

  auto subview(Position pos) const -> PixelsView;

  auto subview(Position pos, Extents extents) const -> PixelsView;

  auto operator[](std::size_t x, std::size_t y) const -> std::uint32_t&;

private:
  std::mdspan<std::uint32_t, Extents, std::layout_stride> mPixels;
};

} // namespace cw