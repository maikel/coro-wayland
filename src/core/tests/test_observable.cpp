// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Observable.hpp"

#include <sync_wait.hpp>

#include <cassert>

class EmptyObservable {
public:
    template <class Tp>
    auto subscribe(ms::ObservableReceiver<Tp>) noexcept -> ms::IoTask<void> {
        co_return;
    }
};

void test_construct_observable()
{
    EmptyObservable emptyObs;
    ms::Observable<int> obs(std::move(emptyObs));
    int counter = 0;
    ms::ObservableReceiver<int> receiver([&](ms::IoTask<int>&&) noexcept -> ms::IoTask<void> {
        ++counter;
        co_return;
    });
    ms::IoTask<void> task = std::move(obs).subscribe(std::move(receiver));
    assert(ms::sync_wait(std::move(task)));
    assert(counter == 0);
}

int main()
{
    test_construct_observable();
}