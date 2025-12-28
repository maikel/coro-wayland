// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

namespace ms {

template <class Fn, class... Args>
concept callable = requires(Fn&& fn, Args&&... args) { fn(static_cast<Args &&>(args)...); };

template <class Awaitable, class Promise>
auto get_awaiter(Awaitable&& awaitable, Promise& promise) {
  if constexpr (requires { promise.await_transform(static_cast<Awaitable &&>(awaitable)); }) {
    return promise.await_transform(static_cast<Awaitable&&>(awaitable));
  } else if constexpr (requires { static_cast<Awaitable &&>(awaitable).operator co_await(); }) {
    return static_cast<Awaitable&&>(awaitable).operator co_await();
  } else {
    return static_cast<Awaitable&&>(awaitable);
  }
}

template <class Awaitable, class Promise>
using awaiter_of_t = decltype(get_awaiter(std::declval<Awaitable>(), std::declval<Promise&>()));

template <class Awaitable, class Promise>
using await_result_t = decltype(std::declval<awaiter_of_t<Awaitable, Promise>>().await_resume());

} // namespace ms