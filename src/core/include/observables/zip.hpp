// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "AsyncQueue.hpp"
#include "Observable.hpp"
#include "when_all.hpp"

#include <tuple>

namespace cw {

template <class... Tps> class ZipObservable {
public:
  static auto do_subscribe(std::function<auto(IoTask<std::tuple<Tps...>>)->IoTask<void>> receiver,
                           Observable<Tps>... observables) -> IoTask<void> {
    std::tuple<AsyncQueue<Tps>...> queues =
        std::make_tuple(co_await use_resource(AsyncQueue<Tps>::make())...);
    auto indices = std::index_sequence_for<Tps...>{};
    auto subscriptions = [&]<std::size_t... I>(std::index_sequence<I...>) {
      return std::make_tuple((observables.subscribe([&](auto valueTask) -> IoTask<void> {
        while (true) {
          Tps value = co_await std::move(valueTask);
          co_await std::get<I>(queues).push(std::move(value));
        }
      }))...);
    }(indices);

    while (true) {
      IoTask<std::tuple<Tps...>> task = [&]() -> IoTask<std::tuple<Tps...>> {
        co_return co_await when_all(std::get<AsyncQueue<Tps>>(queues).pop()...);
      }();
      co_await std::apply(
          [&](auto&&... subs) { return when_all(receiver(std::move(task)), std::move(subs)...); },
          std::move(subscriptions));
    }
  }

  auto
  subscribe(std::function<auto(IoTask<std::tuple<Tps...>>)->IoTask<void>> receiver) const noexcept
      -> IoTask<void> {
    return std::apply(
        [&](auto&&... observables) {
          return do_subscribe(std::move(receiver),
                              std::forward<decltype(observables)>(observables)...);
        },
        mObservables);
  }

private:
  std::tuple<Observable<Tps>...> mObservables;
};

template <class... Tps> auto zip(Observable<Tps>... observables) noexcept -> ZipObservable<Tps...> {
  return ZipObservable<Tps...>{std::make_tuple(std::forward<Observable<Tps>>(observables)...)};
}

} // namespace cw