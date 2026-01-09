// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "wayland/Client.hpp"

#include <mdspan>

namespace cw::wayland {

struct FrameBufferPoolContext;

enum class Width : std::size_t {};
enum class Height : std::size_t {};

class FrameBufferPool {
public:
  static auto make(Client client) -> Observable<FrameBufferPool>;

  auto resize(Width width, Height height) -> IoTask<void>;

  struct BufferView {
    Buffer buffer;
    std::mdspan<std::uint32_t, std::dextents<std::size_t, 2>> pixels;
  };

  auto get_current_buffers() -> Observable<std::array<BufferView, 2>>;

private:
  friend struct FrameBufferPoolContext;
  explicit FrameBufferPool(FrameBufferPoolContext& context) noexcept : mContext(&context) {}
  FrameBufferPoolContext* mContext;
};

} // namespace cw::wayland