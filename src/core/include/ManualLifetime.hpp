// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

namespace ms {

template <class Tp> class ManualLifetime {
public:
  ManualLifetime() noexcept;
  ~ManualLifetime() noexcept;

  template <class... Args> Tp& emplace(Args&&... args);

  void destroy() noexcept;

  auto operator->() noexcept -> Tp*;
  auto operator->() const noexcept -> const Tp*;

private:
  struct Unit {};
  union {
    Unit mDummy;
    Tp mValue;
  } mStorage{};
};

//////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                                ManualLifetime<Tp>

template <class Tp> ManualLifetime<Tp>::ManualLifetime() noexcept = default;

template <class Tp> ManualLifetime<Tp>::~ManualLifetime() noexcept = default;

template <class Tp> template <class... Args> Tp& ManualLifetime<Tp>::emplace(Args&&... args) {
  new (&mStorage.mValue) Tp(static_cast<Args&&>(args)...);
  return mStorage.mValue;
}

template <class Tp> void ManualLifetime<Tp>::destroy() noexcept { mStorage.mValue.~Tp(); }

template <class Tp> auto ManualLifetime<Tp>::operator->() noexcept -> Tp* {
  return &mStorage.mValue;
}

template <class Tp> auto ManualLifetime<Tp>::operator->() const noexcept -> const Tp* {
  return &mStorage.mValue;
}

} // namespace ms