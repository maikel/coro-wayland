// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "BoxConstraints.hpp"

#include <memory>
#include <vector>

namespace cw {

class RenderContext;

// Abstract base class for all UI widgets
// Follows Flutter's constraint-based layout model
class Widget {
public:
  virtual ~Widget() = default;

  // Layout phase: given constraints, calculate and return size
  // Must respect constraints (return size within min/max bounds)
  virtual auto layout(BoxConstraints constraints) -> Size = 0;

  // Render phase: draw self and children to context
  virtual auto render(RenderContext& context) -> void = 0;

  // Dirty flag management
  auto mark_dirty() -> void { mDirty = true; }
  auto is_dirty() const -> bool { return mDirty; }
  auto mark_clean() -> void { mDirty = false; }

  // Get current size (valid after layout)
  auto size() const -> Size { return mSize; }

protected:
  // Set size during layout
  auto set_size(Size size) -> void { mSize = size; }

private:
  Size mSize{0, 0};
  Offset mOffset{0, 0};
  bool mDirty = true;
};

// Multi-child widget base class
class MultiChildWidget : public Widget {
public:
  auto add_child(std::unique_ptr<Widget> child) -> void { mChildren.push_back(std::move(child)); }

  auto children() -> std::vector<std::unique_ptr<Widget>>& { return mChildren; }
  auto children() const -> std::vector<std::unique_ptr<Widget>> const& { return mChildren; }

protected:
  std::vector<std::unique_ptr<Widget>> mChildren;
};

} // namespace cw
