// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <any>

namespace cw {

template <class BaseT> class Polymorphic {
public:
  Polymorphic() = default;

  Polymorphic(Polymorphic const&) = default;
  Polymorphic& operator=(Polymorphic const&) = default;
  Polymorphic(Polymorphic&&) = default;
  Polymorphic& operator=(Polymorphic&&) = default;

  ~Polymorphic() = default;

  template <class DerivedT>
    requires std::derived_from<DerivedT, BaseT>
  Polymorphic(DerivedT object) : mObject(std::move(object)) {
    mGetBasePtr = [](std::any& any) -> BaseT* { return &std::any_cast<DerivedT&>(any); };
  }

  auto get() noexcept -> BaseT* {
    if (mGetBasePtr) {
      return mGetBasePtr(mObject);
    } else {
      return nullptr;
    }
  }

  auto get() const noexcept -> const BaseT* {
    if (mGetBasePtr) {
      return mGetBasePtr(const_cast<std::any&>(mObject));
    } else {
      return nullptr;
    }
  }

  auto operator->() noexcept -> BaseT* { return get(); }

  auto operator->() const noexcept -> const BaseT* { return get(); }

private:
  std::any mObject;
  BaseT* (*mGetBasePtr)(std::any&);
};

} // namespace cw