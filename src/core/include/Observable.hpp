// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "IoTask.hpp"

#include <any>
#include <concepts>

namespace ms {

template <class Tp> class ObservableReceiver {
public:
  ObservableReceiver() = delete;

  ObservableReceiver(const ObservableReceiver&) = default;
  ObservableReceiver& operator=(const ObservableReceiver&) = default;
  ObservableReceiver(ObservableReceiver&&) = default;
  ObservableReceiver& operator=(ObservableReceiver&&) = default;
  ~ObservableReceiver() = default;

  template <class Receiver>
    requires(!std::same_as<std::decay_t<Receiver>, ObservableReceiver> &&
             std::constructible_from<std::decay_t<Receiver>, Receiver> &&
             requires(std::decay_t<Receiver>& r, IoTask<Tp>&& task) {
               { r.next(std::move(task)) } noexcept;
             })
  ObservableReceiver(Receiver&& receiver) noexcept : mValue(std::forward<Receiver>(receiver)) {
    mNextFunc = [](std::any& value, IoTask<Tp>&& task) noexcept -> IoTask<void> {
      Receiver& r = std::any_cast<Receiver&>(value);
      return r.next(std::move(task));
    };
  }

  template <class Fn>
    requires(requires(Fn& fn, IoTask<Tp>&& task) {
               { fn(std::move(task)) } noexcept -> std::same_as<IoTask<void>>;
             })
  ObservableReceiver(Fn&& fn) noexcept : mValue(std::forward<Fn>(fn)) {
    mNextFunc = [](std::any& value, IoTask<Tp>&& task) noexcept -> IoTask<void> {
      Fn& f = std::any_cast<Fn&>(value);
      return f(std::move(task));
    };
  }

  auto next(IoTask<Tp>&& task) noexcept -> IoTask<void> {
    return mNextFunc(mValue, std::move(task));
  }

private:
  std::any mValue;
  auto (*mNextFunc)(std::any&, IoTask<Tp>&&) noexcept -> IoTask<void> = nullptr;
};

template <class Tp> class Observable {
public:
  Observable() = delete;

  Observable(const Observable&) = delete;
  Observable& operator=(const Observable&) = delete;
  Observable(Observable&&) = default;
  Observable& operator=(Observable&&) = default;
  ~Observable() = default;

  template <class ObservableLike>
    requires(!std::same_as<std::decay_t<ObservableLike>, Observable> &&
             std::constructible_from<std::decay_t<ObservableLike>, ObservableLike> &&
             requires(std::decay_t<ObservableLike>& obs, ObservableReceiver<Tp>&& receiver) {
               {
                 std::move(obs).subscribe(std::move(receiver))
               } noexcept -> std::same_as<IoTask<void>>;
             })
  explicit Observable(ObservableLike&& observable) noexcept
      : mValue(std::forward<ObservableLike>(observable)) {
    mSubscribeFunc = [](std::any& value,
                        ObservableReceiver<Tp>&& receiver) noexcept -> IoTask<void> {
      ObservableLike& ob = std::any_cast<ObservableLike&>(value);
      return std::move(ob).subscribe(std::move(receiver));
    };
  }

  auto subscribe(ObservableReceiver<Tp> receiver) && noexcept -> IoTask<void> {
    return mSubscribeFunc(mValue, std::move(receiver));
  }

private:
  std::any mValue;
  auto (*mSubscribeFunc)(std::any&, ObservableReceiver<Tp>&&) noexcept -> IoTask<void> = nullptr;
};

} // namespace ms