// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <concepts>
#include <type_traits>
#include <utility>
#include <variant>

namespace cw {

template <class ValueType> struct ValueOrMonostateType {
  using type = ValueType;
};

template <> struct ValueOrMonostateType<void> {
  using type = std::monostate;
};

template <class Fn, class... Args>
concept callable = requires(Fn&& fn, Args&&... args) { fn(static_cast<Args &&>(args)...); };

struct ConnectablePromise {
  template <class Self, class Expression>
  auto await_transform(this Self& self, Expression&& expr) -> decltype(auto) {
    if constexpr (requires { std::forward<Expression>(expr).connect(self); }) {
      return std::forward<Expression>(expr).connect(self);
    } else if constexpr (requires { std::forward<Expression>(expr).operator co_await(); }) {
      return std::forward<Expression>(expr).operator co_await();
    } else {
      return std::forward<Expression>(expr);
    }
  }
};

template <class Env> struct PromiseWithEnv : ConnectablePromise {
  auto get_env() const noexcept -> Env;
};

template <class Awaitable, class Promise>
auto get_awaiter(Awaitable&& awaitable, Promise& promise) -> decltype(auto) {
  if constexpr (requires { promise.await_transform(static_cast<Awaitable &&>(awaitable)); }) {
    return promise.await_transform(static_cast<Awaitable&&>(awaitable));
  } else if constexpr (requires { static_cast<Awaitable &&>(awaitable).operator co_await(); }) {
    return static_cast<Awaitable&&>(awaitable).operator co_await();
  } else {
    return static_cast<Awaitable&&>(awaitable);
  }
}

template <class Awaitable, class Promise>
using awaiter_of_t =
    decltype(::cw::get_awaiter(std::declval<Awaitable&&>(), std::declval<Promise&>()));

template <class Awaitable, class Promise>
using await_result_t = decltype(std::declval<awaiter_of_t<Awaitable, Promise>>().await_resume());

} // namespace cw