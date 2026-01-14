// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "Font.hpp"
#include "Widget.hpp"

#include <string>

namespace cw {

struct TextProperties {
  std::string text;
  std::uint32_t color;
  Font font;
};

// Leaf widget that displays text
class Text : public Widget {
public:
  explicit Text(Observable<TextProperties>&& properties);
  explicit Text(Font const& font, std::string text, std::uint32_t color);

  auto render_object() && -> Observable<AnyRenderObject> override;

private:
  Observable<TextProperties> mProperties;
};

} // namespace cw
