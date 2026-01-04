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
    mNextFunc = [](std::any& value, IoTask<Tp> task) noexcept -> IoTask<void> {
      Receiver& r = std::any_cast<Receiver&>(value);
      return r.next(std::move(task));
    };
  }

  template <class Fn>
    requires(requires(Fn& fn, IoTask<Tp>&& task) {
               { fn(std::move(task)) } noexcept -> std::same_as<IoTask<void>>;
             })
  ObservableReceiver(Fn&& fn) noexcept : mValue(std::forward<Fn>(fn)) {
    mNextFunc = [](std::any& value, IoTask<Tp> task) noexcept -> IoTask<void> {
      Fn& f = std::any_cast<Fn&>(value);
      return f(std::move(task));
    };
  }

  auto next(IoTask<Tp> task) noexcept -> IoTask<void> {
    return mNextFunc(mValue, std::move(task));
  }

private:
  std::any mValue;
  auto (*mNextFunc)(std::any&, IoTask<Tp>) noexcept -> IoTask<void> = nullptr;
};

template <class Tp> class Observable {
private:
  struct Model {
    virtual ~Model() = default;
    virtual auto subscribe(ObservableReceiver<Tp> receiver) noexcept -> IoTask<void> = 0;
  };
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
  Observable(ObservableLike&& observable) noexcept {
    struct ObserverLikeModel final : Model {
      explicit ObserverLikeModel(ObservableLike&& obs) noexcept
          : mObservable(std::forward<ObservableLike>(obs)) {}
      auto subscribe(ObservableReceiver<Tp> receiver) noexcept -> IoTask<void> override {
        return std::move(mObservable).subscribe(std::move(receiver));
      }
    private:
      ObservableLike mObservable;
    };
    mValue = std::make_unique<ObserverLikeModel>(std::forward<ObservableLike>(observable));
  }

  auto subscribe(ObservableReceiver<Tp> receiver) && noexcept -> IoTask<void> {
    auto value{std::move(mValue)};
    return value->subscribe(std::move(receiver));
  }

private:
  std::unique_ptr<Model> mValue;
};

} // namespace ms