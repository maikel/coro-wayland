// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "Widget.hpp"

#include <memory>
#include <optional>

namespace cw {

struct Color;

// Container widget with optional background color and single child
// Similar to Flutter's Container (simplified version)
class Container : public Widget {
public:
  Container();

  auto layout(BoxConstraints constraints) -> Size override;
  auto render(RenderContext& context) -> void override;

  // Set child widget
  auto set_child(std::unique_ptr<Widget> child) -> void;

  // Set background color
  auto set_background_color(Color color) -> void;

  // Set explicit width/height (nullopt = use child size)
  auto set_width(std::optional<std::size_t> width) -> void;
  auto set_height(std::optional<std::size_t> height) -> void;

private:
  std::unique_ptr<Widget> mChild;
  std::optional<Color> mBackgroundColor;
  std::optional<std::size_t> mWidth;
  std::optional<std::size_t> mHeight;
};

} // namespace cw
