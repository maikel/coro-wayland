// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "AsyncScope.hpp"
#include "Observable.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>

#include <sys/socket.h>

namespace ms::wayland {

struct FileDescriptorHandle {
public:
  explicit FileDescriptorHandle(int handle);

  auto native_handle() const noexcept -> int;

  friend auto operator<=>(FileDescriptorHandle, FileDescriptorHandle) = default;

private:
  int mNativeHandle;
};

enum class ObjectId : std::uint32_t {
  Display = 0,
};

enum class OpCode : std::uint16_t {};

class ProxyInterface;
class Connection;

class ConnectionHandle {
public:
  explicit ConnectionHandle(Connection* connection) noexcept : mConnection(connection) {}

  template <class... Args>
  auto send_message(ObjectId objectId, OpCode opCode, const Args&... args) -> void;

  template <class... Args>
  auto read_message(std::span<const char> buffer, Args&... args) -> std::size_t;

  auto read_next_file_descriptor() -> FileDescriptorHandle;

  template <class InterfaceType> auto create_interface() -> InterfaceType;

private:
  friend class ProxyInterface;
  auto register_interface(ProxyInterface* proxy) -> void;

  Connection* mConnection;
};

class ProxyInterface : ImmovableBase {
public:
  explicit ProxyInterface(ObjectId objectId, ConnectionHandle handle) noexcept
      : mObjectId(objectId), mHandle(handle) {
    mHandle.register_interface(this);
  }

  virtual ~ProxyInterface() = default;
  virtual auto handle_message(std::span<const char> message) -> IoTask<void> = 0;

  auto get_object_id() const noexcept -> ObjectId { return mObjectId; }

  auto get_connection() noexcept -> ConnectionHandle { return mHandle; }

private:
  ObjectId mObjectId;
  ConnectionHandle mHandle;
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
  std::atomic<std::uint32_t> mNextObjectId{1};
  int mFd;
  std::unordered_map<ObjectId, ProxyInterface*> mProxies;
};

template <class InterfaceType> auto ConnectionHandle::create_interface() -> InterfaceType {
  ObjectId objectId = static_cast<ObjectId>(mConnection->mNextObjectId.fetch_add(1));
  return InterfaceType{objectId, *this};
}

} // namespace ms::wayland