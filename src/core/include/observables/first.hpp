// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "Observable.hpp"
#include "just_stopped.hpp"
#include "stopped_as_optional.hpp"

#include <optional>

namespace cw::observables {

template <class T> auto first(Observable<T>&& ob) -> IoTask<T> {
  std::optional<T> mValue;
  while (true) {
    co_await stopped_as_optional(std::move(ob).subscribe([&](auto valueTask) -> IoTask<void> {
      mValue.emplace(co_await std::move(valueTask));
      co_await just_stopped();
    }));
    if (mValue) {
      co_return std::move(mValue).value();
    }
    co_await just_stopped();
  }
}

} // namespace cw::observables