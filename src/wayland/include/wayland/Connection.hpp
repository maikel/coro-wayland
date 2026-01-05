// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "AsyncScope.hpp"
#include "Observable.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>

namespace ms::wayland {

class ConnectionHandle;

enum class ObjectId : std::uint32_t {
  Display = 0,
};

enum class OpCode : std::uint16_t {
};

class ProxyBase {
public:
  explicit ProxyBase(ObjectId objectId) noexcept : mObjectId(objectId) {}

  virtual ~ProxyBase() = default;
  virtual auto handle_message(std::span<const char> message) -> IoTask<void> = 0;

protected:
  ObjectId mObjectId;
};

class Connection {
public:
  Connection();
  ~Connection();

  auto get_fd() const noexcept -> int { return mFd; }

  auto close() -> Task<void>;

  auto run() -> Observable<ConnectionHandle>;

  friend class ConnectionHandle;

  AsyncScope mScope;
  int mFd;
  std::unordered_map<ObjectId, ProxyBase*> mProxies;
};

class ConnectionHandle {
public:
  explicit ConnectionHandle(Connection* connection) noexcept : mConnection(connection) {}

  template <class... Args>
  auto send_message(ObjectId objectId, OpCode opCode, Args&&... args) -> void;

  auto register_proxy(ProxyBase* proxy) -> void;

private:
  Connection* mConnection;
};

} // namespace ms::wayland