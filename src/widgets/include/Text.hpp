// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "Widget.hpp"

#include <string>

namespace cw {

class Font;
struct Color;

// Leaf widget that displays text
class Text : public Widget {
public:
  Text(Font const& font, std::string text, Color color);

  auto layout(BoxConstraints constraints) -> Size override;
  auto render(RenderContext& context) -> void override;

  // Update text content and mark dirty
  auto set_text(std::string text) -> void;

private:
  Font const* mFont;
  std::string mText;
  Color mColor;
};

} // namespace cw
