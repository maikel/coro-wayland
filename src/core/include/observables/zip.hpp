// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "AsyncQueue.hpp"
#include "when_all.hpp"
#include "Observable.hpp"

#include <tuple>

namespace cw {

template <class... Tps> class ZipObservable {
public:
  static do_subscribe(std::function<auto(IoTask<std::tuple<Tps...>>)->IoTask<void>> receiver,
                      Observable<Tps>... observables) -> IoTask<void> {
    std::tuple<AsyncQueue<Tps>...> queues =
        std::make_tuple(co_await use_resource(AsyncQueue<Tps>::make())...);
    while (true) {
      IoTask<std::tuple<Tps...>> task = [&]() -> IoTask<std::tuple<Tps...>> {
        co_return co_await when_all(std::get<AsyncQueue<Tps>>(queues).pop()...);
      }();
      co_await receiver(std::move(task));
    }
    co_return;
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

} // namespace cw