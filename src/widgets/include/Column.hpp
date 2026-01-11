// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "Widget.hpp"

namespace cw {

enum class MainAxisAlignment {
  Start,        // Align children to the start
  End,          // Align children to the end
  Center,       // Center children
  SpaceBetween, // Space evenly between children
  SpaceAround,  // Space evenly around children
};

enum class CrossAxisAlignment {
  Start,   // Align to start of cross axis
  End,     // Align to end of cross axis
  Center,  // Center on cross axis
  Stretch, // Stretch to fill cross axis
};

// Vertical flex layout widget
// Follows Flutter's Column behavior with Flexible/Expanded children
class Column : public MultiChildWidget {
public:
  Column();

  auto layout(BoxConstraints constraints) -> Size override;
  auto render(RenderContext& context) -> void override;

  auto set_main_axis_alignment(MainAxisAlignment alignment) -> void;
  auto set_cross_axis_alignment(CrossAxisAlignment alignment) -> void;

private:
  MainAxisAlignment mMainAxisAlignment = MainAxisAlignment::Start;
  CrossAxisAlignment mCrossAxisAlignment = CrossAxisAlignment::Center;
};

} // namespace cw
