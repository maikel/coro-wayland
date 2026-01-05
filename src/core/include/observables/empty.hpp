// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "Observable.hpp"

namespace ms::observables {

class EmptyObservable {
public:
  auto subscribe(auto) noexcept -> ms::IoTask<void> { co_return; }
};

inline auto empty() noexcept -> EmptyObservable { return EmptyObservable{}; }

} // namespace ms::observables