// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "coro_just.hpp"
#include "observables/use_resource.hpp"
#include "wayland/Connection.hpp"
#include "wayland/protocol.hpp"

namespace ms::wayland {

struct ClientContext;

class Client {
public:
  static auto make() -> Observable<Client>;

  template <class GlobalInterface> auto bind() -> Observable<GlobalInterface>;

  auto connection() -> Connection;

  auto events() -> Observable<wayland::Display::ErrorEvent>;

private:
  auto get_next_object_id() -> ObjectId;
  auto find_global(std::string_view interface) -> IoTask<Registry::GlobalEvent>;
  auto bind_global(const Registry::GlobalEvent& global, ObjectId new_id) -> void;

  friend struct ClientContext;
  explicit Client(ClientContext& context) noexcept : mContext(&context) {}
  ClientContext* mContext;
};

template <class GlobalInterface> auto Client::bind() -> Observable<GlobalInterface> {
  struct BindObservable {
    static auto do_subscribe(Client client,
                             std::function<auto(IoTask<GlobalInterface>)->IoTask<void>> receiver)
        -> IoTask<void> {
      Registry::GlobalEvent global = co_await client.find_global(GlobalInterface::interface_name());
      ObjectId new_id = client.get_next_object_id();
      GlobalInterface interface =
          co_await use_resource(GlobalInterface::make(new_id, client.connection()));
      client.bind_global(global, new_id);
      co_await receiver(coro_just(interface));
    }

    auto
    subscribe(std::function<auto(IoTask<GlobalInterface>)->IoTask<void>> receiver) const noexcept
        -> IoTask<void> {
      return do_subscribe(mClient, std::move(receiver));
    }

    Client mClient;
  };
  return BindObservable{*this};
}

} // namespace ms::wayland