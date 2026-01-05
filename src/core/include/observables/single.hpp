// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "Observable.hpp"

namespace ms::observables {

template <class Sender> class SingleObservable {
public:
  explicit SingleObservable(Sender&& sender) noexcept : mSender(std::forward<Sender>(sender)) {}

  template <class Self, class Receiver>
  auto subscribe(this Self&& self, Receiver receiver) noexcept -> IoTask<void> {
    return [](Receiver r, Sender sndr) -> IoTask<void> {
      co_await std::move(r)(std::move(sndr));
    }(std::move(receiver), std::forward<Self>(self).mSender);
  }

private:
  Sender mSender;
};

template <class Sender>
auto single(Sender&& sender) noexcept -> SingleObservable<std::decay_t<Sender>> {
  return SingleObservable<std::decay_t<Sender>>(std::forward<Sender>(sender));
}

} // namespace ms::observables