// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

namespace cw {

template <class Tp> class ManualLifetime {
public:
  ManualLifetime() noexcept;
  ~ManualLifetime() noexcept;

  template <class... Args> Tp& emplace(Args&&... args);

  void destroy() noexcept;

  auto get() -> Tp*;

  auto operator->() noexcept -> Tp*;
  auto operator->() const noexcept -> const Tp*;

private:
  alignas(Tp) unsigned char mStorage[sizeof(Tp)];
};

//////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                                ManualLifetime<Tp>

template <class Tp> ManualLifetime<Tp>::ManualLifetime() noexcept = default;

template <class Tp> ManualLifetime<Tp>::~ManualLifetime() noexcept = default;

template <class Tp> template <class... Args> Tp& ManualLifetime<Tp>::emplace(Args&&... args) {
  Tp* pointer = new (mStorage) Tp(static_cast<Args&&>(args)...);
  return *pointer;
}

template <class Tp> void ManualLifetime<Tp>::destroy() noexcept {
  reinterpret_cast<Tp*>(mStorage)->~Tp();
}

template <class Tp> auto ManualLifetime<Tp>::get() -> Tp* {
  return reinterpret_cast<Tp*>(mStorage);
}

template <class Tp> auto ManualLifetime<Tp>::operator->() noexcept -> Tp* {
  return reinterpret_cast<Tp*>(mStorage);
}

template <class Tp> auto ManualLifetime<Tp>::operator->() const noexcept -> const Tp* {
  return reinterpret_cast<const Tp*>(mStorage);
}

} // namespace cw