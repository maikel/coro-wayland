// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "observables/use_resource.hpp"
#include "sync_wait.hpp"
#include "wayland/Client.hpp"

using namespace ms;

auto coro_main() -> IoTask<void> {
  wayland::Client client = co_await use_resource(wayland::Client::make());
  wayland::Shm shm = co_await use_resource(client.bind<wayland::Shm>());
}

int main() { sync_wait(coro_main()); }