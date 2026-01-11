// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "Widget.hpp"

#include <memory>

namespace cw {

enum class FlexFit {
  Tight, // Child must fill allocated space (Expanded behavior)
  Loose, // Child can be smaller than allocated space
};

// Wrapper widget that marks child as flexible within Row/Column
// Parent (Row/Column) checks for this type to distribute remaining space
class Flexible : public Widget {
public:
  Flexible(std::unique_ptr<Widget> child, int flex = 1, FlexFit fit = FlexFit::Loose);

  auto layout(BoxConstraints constraints) -> Size override;
  auto render(RenderContext& context) -> void override;

  auto flex() const -> int { return mFlex; }
  auto fit() const -> FlexFit { return mFit; }
  auto child() -> Widget* { return mChild.get(); }

private:
  std::unique_ptr<Widget> mChild;
  int mFlex;
  FlexFit mFit;
};

// Helper function to create Expanded widget (Flexible with tight fit)
inline auto Expanded(std::unique_ptr<Widget> child, int flex = 1) -> std::unique_ptr<Flexible> {
  return std::make_unique<Flexible>(std::move(child), flex, FlexFit::Tight);
}

} // namespace cw
