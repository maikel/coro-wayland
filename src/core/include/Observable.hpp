// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "IoTask.hpp"

#include <any>
#include <concepts>

namespace ms {

template <class Tp> class Observable {
public:
  using ObservableReceiver = std::function<auto(IoTask<Tp>)->IoTask<void>>;

private:
  struct Model {
    virtual ~Model() = default;
    virtual auto subscribe(ObservableReceiver receiver) noexcept -> IoTask<void> = 0;
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
             requires(std::decay_t<ObservableLike>& obs, ObservableReceiver receiver) {
               {
                 std::move(obs).subscribe(std::move(receiver))
               } noexcept -> std::same_as<IoTask<void>>;
             })
  Observable(ObservableLike&& observable) noexcept {
    struct ObserverLikeModel final : Model {
      explicit ObserverLikeModel(ObservableLike&& obs) noexcept
          : mObservable(std::forward<ObservableLike>(obs)) {}
      auto subscribe(ObservableReceiver receiver) noexcept -> IoTask<void> override {
        return std::move(mObservable).subscribe(std::move(receiver));
      }

    private:
      ObservableLike mObservable;
    };
    mValue = std::make_unique<ObserverLikeModel>(std::forward<ObservableLike>(observable));
  }

  auto subscribe(ObservableReceiver receiver) && noexcept -> IoTask<void> {
    auto value{std::move(mValue)};
    return value->subscribe(std::move(receiver));
  }

private:
  std::unique_ptr<Model> mValue;
};

} // namespace ms