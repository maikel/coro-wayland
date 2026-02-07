// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "Observable.hpp"

#include <functional>

namespace cw::observables {

template <class T, class Func>
auto then(Observable<T>&& ob, Func&& func) -> Observable<std::invoke_result_t<Func, T>> {
  using ResultT = std::invoke_result_t<std::remove_cvref_t<Func>&, T>;
  struct ThenObservable {
    Observable<T> mSource;
    std::remove_cvref_t<Func> mFunc;

    static auto do_subscribe(Observable<T> source, std::remove_cvref_t<Func> func,
                             std::function<auto(IoTask<ResultT>)->IoTask<void>> receiver)
        -> IoTask<void> {
      co_await std::move(source).subscribe([&](IoTask<T> valueTask) -> IoTask<void> {
        co_await receiver([](Func& func, IoTask<T> valueTask) -> IoTask<ResultT> {
          T value = co_await std::move(valueTask);
          co_return func(std::move(value));
        }(func, std::move(valueTask)));
      });
    }

    auto subscribe(std::function<auto(IoTask<ResultT>)->IoTask<void>> receiver) && noexcept
        -> IoTask<void> {
      return do_subscribe(std::move(mSource), std::move(mFunc), std::move(receiver));
    }
  };
  return ThenObservable{std::move(ob), std::forward<Func>(func)};
}

} // namespace cw::observables
