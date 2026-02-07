// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "BoxConstraints.hpp"
#include "Observable.hpp"
#include "PixelsView.hpp"
#include "Polymoprhic.hpp"

#include <memory>
#include <vector>

namespace cw {

class RenderContext;

// Abstract base class for all UI widgets
// Follows Flutter's constraint-based layout model
class RenderObject {
public:
  // Layout phase: given constraints, calculate and return size
  // Must respect constraints (return size within min/max bounds)
  virtual auto layout(const RenderContext& context, BoxConstraints constraints)
      -> BoxConstraints = 0;

  // Render phase: draw self and children to context
  // redraw indicates if full redraw is needed
  // returns list of regions that were updated
  virtual auto render(RenderContext& context, bool redraw = false) -> std::vector<Region> = 0;

  // Observable that emits when the widget needs to be redrawn
  virtual auto dirty() const -> Observable<void> = 0;

protected:
  ~RenderObject() = default;
};

using AnyRenderObject = Polymorphic<RenderObject>;

class Widget {
public:
  virtual ~Widget() = default;

  virtual auto render_object() && -> Observable<AnyRenderObject> = 0;
};

class AnyWidget {
public:
  AnyWidget() = default;

  AnyWidget(const AnyWidget&) = delete;
  AnyWidget& operator=(const AnyWidget&) = delete;

  AnyWidget(AnyWidget&&) = default;
  AnyWidget& operator=(AnyWidget&&) = default;

  template <class WidgetT>
    requires std::derived_from<WidgetT, Widget>
  AnyWidget(WidgetT widget) : mWidget(std::make_unique<WidgetT>(std::move(widget))) {}

  auto render_object() && -> Observable<AnyRenderObject> {
    return std::move(*mWidget).render_object();
  }

private:
  std::unique_ptr<Widget> mWidget;
};

} // namespace cw
