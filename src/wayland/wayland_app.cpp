// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "read_env.hpp"
#include "sync_wait.hpp"
#include "queries.hpp"
#include "wayland/Connection.hpp"

#include <iostream>

int main() {
  ms::wayland::Connection connection;
  ms::sync_wait(connection.run().subscribe([](auto handle) noexcept -> ms::IoTask<void> {
    ms::wayland::ConnectionHandle connHandle = co_await std::move(handle);
    std::cout << "Connected to Wayland server!" << std::endl;
    ms::IoScheduler scheduler = co_await ms::read_env(ms::get_scheduler);
    co_await scheduler.schedule_after(std::chrono::years(1));
  }));
}