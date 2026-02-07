// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "PixelsView.hpp"

namespace cw {

PixelsView::PixelsView(std::span<std::uint32_t> data, Extents extents) noexcept
    : mPixels(data.data(), std::layout_stride::mapping<Extents>{
                               extents, std::array<std::size_t, 2>{1, extents.extent(0)}}) {}

PixelsView::PixelsView(
    std::mdspan<std::uint32_t, std::dextents<std::size_t, 2>, std::layout_stride> pixels) noexcept
    : mPixels(pixels) {}

auto PixelsView::width() const -> std::size_t { return mPixels.extent(0); }
auto PixelsView::height() const -> std::size_t { return mPixels.extent(1); }

auto PixelsView::extents() const -> Extents { return mPixels.extents(); }

auto PixelsView::data() const -> std::uint32_t* { return mPixels.data_handle(); }

auto PixelsView::row_stride() const -> std::size_t { return mPixels.stride(1); }

auto PixelsView::subview(Position pos) const -> PixelsView
{
  return this->subview(pos, Extents{width() - pos.x, height() - pos.y});
}

auto PixelsView::subview(Position pos, Extents extents) const -> PixelsView {
  if (pos.x + extents.extent(0) > width() || pos.y + extents.extent(1) > height()) {
    throw std::out_of_range("Subview extents exceed parent view bounds");
  }
  std::uint32_t* data = mPixels.data_handle() + (pos.y * mPixels.stride(1)) + (pos.x * mPixels.stride(0));
  std::layout_stride::mapping<Extents> subviewMapping{
      extents, std::array{mPixels.stride(0), mPixels.stride(1)}};
  std::mdspan<std::uint32_t, Extents, std::layout_stride> newPixels{data, subviewMapping};
  return PixelsView{newPixels};
}

auto PixelsView::operator[](std::size_t x, std::size_t y) const -> std::uint32_t& {
  return mPixels[x, y];
}

} // namespace cw