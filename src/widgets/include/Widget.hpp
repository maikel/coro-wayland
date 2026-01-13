// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "BoxConstraints.hpp"
#include "Observable.hpp"

#include <memory>
#include <vector>

namespace cw {

class RenderContext;

struct Region {
  Position position;
  Extents extents;

  auto operator==(Region const&) const -> bool = default;
};

// Abstract base class for all UI widgets
// Follows Flutter's constraint-based layout model
class Widget {
public:
  virtual ~Widget() = default;

  // Layout phase: given constraints, calculate and return size
  // Must respect constraints (return size within min/max bounds)
  virtual auto layout(BoxConstraints constraints) -> BoxConstraints = 0;

  // Render phase: draw self and children to context
  // redraw indicates if full redraw is needed
  // returns list of regions that were updated
  virtual auto render(RenderContext& context, bool redraw = false) -> std::vector<Region> = 0;

  // Observable that emits when the widget needs to be redrawn
  virtual auto dirty() const -> Observable<void> = 0;
};

} // namespace cw
