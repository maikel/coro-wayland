// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <concepts>

namespace cw {

struct NarrowError : public std::runtime_error {
  NarrowError() : std::runtime_error("narrowing error") {}
};

/**
 * @brief This function is used to convert integer values to smaller integer types.
 *
 * If the value cannot be represented by the smaller type, an exception is thrown.
 * For example, if the value is negative and the smaller type is unsigned, an exception is thrown.
 */
template <class T, class U>
  requires std::integral<T> && std::integral<U>
auto narrow(U u) -> T {
  static constexpr bool isDifferentSignedness = std::is_signed_v<T> != std::is_signed_v<U>;
  T t = static_cast<T>(u);
  if (static_cast<U>(t) != u || (isDifferentSignedness && ((t < T{}) != (u < U{})))) {
    throw NarrowError();
  }
  return t;
}

} // namespace cw