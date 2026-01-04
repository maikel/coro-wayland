// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "Observable.hpp"

namespace ms::observables {

class EmptyObservable {
public:
    template <class Tp>
    auto subscribe(ms::ObservableReceiver<Tp>) noexcept -> ms::IoTask<void> {
        co_return;
    }
};

inline auto empty() noexcept -> EmptyObservable {
    return EmptyObservable{};
}

} // namespace ms::observables