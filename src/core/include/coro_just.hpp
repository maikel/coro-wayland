// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "IoTask.hpp"

namespace cw {

template <class ValueT> auto coro_just(ValueT value) -> IoTask<ValueT> {
  co_return std::move(value);
}

inline auto coro_just_void() -> IoTask<void> { co_return; }

template <class Sender> auto ignore_result(Sender sndr) -> IoTask<void> {
  co_await std::move(sndr);
  co_return;
}

} // namespace cw