// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "AsyncScope.hpp"
#include "FileDescriptor.hpp"
#include "Observable.hpp"

#include <cstdint>
#include <queue>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/socket.h>

namespace cw::wayland {

enum class ObjectId : std::uint32_t {
  Display = 1,
};

enum class OpCode : std::uint16_t {};

class ProxyInterface;
class ConnectionContext;

class Connection {
public:
  static auto make() -> Observable<Connection>;

  auto get_scheduler() const noexcept -> IoScheduler;
  auto get_next_object_id() const noexcept -> ObjectId;

private:
  friend class ConnectionContext;
  friend class ProxyInterface;

  explicit Connection(ConnectionContext* connection) noexcept;

  template <class... Args>
  auto send_message(ObjectId objectId, OpCode opCode, const Args&... args) -> void;

  template <class... Args>
  auto read_message(std::span<const char> buffer, Args&... args) -> std::size_t;

  auto read_next_file_descriptor() -> FileDescriptorHandle;

  auto proxy_from_object_id(ObjectId objectId) -> ProxyInterface*;

  template <class InterfaceType>
    requires requires { typename InterfaceType::context_type; }
  auto from_object_id(ObjectId objectId) -> InterfaceType;

  auto unregister_interface(ProxyInterface* proxy) -> void;
  auto register_interface(ProxyInterface* proxy) -> void;

  void send_message(std::vector<char> message, std::optional<FileDescriptorHandle> fds);

  auto message_length(const std::string& arg) -> std::uint16_t;
  auto message_length(std::span<const char> arg) -> std::uint16_t;
  auto message_length(std::int32_t) -> std::uint16_t;
  auto message_length(std::uint32_t) -> std::uint16_t;
  auto message_length(ObjectId) -> std::uint16_t;
  auto message_length(FileDescriptorHandle) -> std::uint16_t;

  template <class InterfaceType>
    requires requires { typename InterfaceType::context_type; }
  auto message_length(InterfaceType) -> std::uint16_t;

  auto put_arg_to_message(std::span<char> buffer, const std::string& arg) -> std::span<char>;
  auto put_arg_to_message(std::span<char> buffer, std::span<const char> arg) -> std::span<char>;
  auto put_arg_to_message(std::span<char> buffer, std::int32_t arg) -> std::span<char>;
  auto put_arg_to_message(std::span<char> buffer, std::uint32_t arg) -> std::span<char>;
  auto put_arg_to_message(std::span<char> buffer, ObjectId arg) -> std::span<char>;
  auto put_arg_to_message(std::span<char> buffer, FileDescriptorHandle arg) -> std::span<char>;

  template <class InterfaceType>
    requires requires { typename InterfaceType::context_type; }
  auto put_arg_to_message(std::span<char> buffer, InterfaceType arg) -> std::span<char>;

  auto extract_arg_from_message(std::span<const char> buffer, std::string& arg)
      -> std::span<const char>;
  auto extract_arg_from_message(std::span<const char> buffer, std::vector<char>& arg)
      -> std::span<const char>;
  auto extract_arg_from_message(std::span<const char> buffer, std::int32_t& arg)
      -> std::span<const char>;
  auto extract_arg_from_message(std::span<const char> buffer, std::uint32_t& arg)
      -> std::span<const char>;
  auto extract_arg_from_message(std::span<const char> buffer, ObjectId& arg)
      -> std::span<const char>;
  auto extract_arg_from_message(std::span<const char> buffer, FileDescriptorHandle& arg)
      -> std::span<const char>;

  template <class InterfaceType>
    requires requires { typename InterfaceType::context_type; }
  auto extract_arg_from_message(std::span<const char> buffer, InterfaceType arg)
      -> std::span<const char>;

  ConnectionContext* mConnection;
};

class ProxyInterface : ImmovableBase {
public:
  explicit ProxyInterface(ObjectId objectId, Connection handle) noexcept
      : mObjectId(objectId), mHandle(handle) {
    mHandle.register_interface(this);
  }

  virtual ~ProxyInterface() { mHandle.unregister_interface(this); }

  virtual auto handle_message(std::span<const char> message, OpCode code) -> IoTask<void> = 0;

  auto get_object_id() const noexcept -> ObjectId { return mObjectId; }

  auto get_connection() noexcept -> Connection { return mHandle; }

  template <class... Args> auto send_message(OpCode opCode, const Args&... args) -> void;

  template <class... Args>
  auto read_message(std::span<const char> buffer, Args&... args) -> std::size_t;

private:
  ObjectId mObjectId;
  Connection mHandle;
};

template <class... Args>
auto Connection::read_message(std::span<const char> buffer, Args&... args) -> std::size_t {
  std::span<const char> remainingBuffer = buffer.subspan(2 * sizeof(std::uint32_t)); // skip header
  ((remainingBuffer = extract_arg_from_message(remainingBuffer, args)), ...);
  return buffer.size() - remainingBuffer.size();
}

template <class InterfaceType>
  requires requires { typename InterfaceType::context_type; }
auto Connection::from_object_id(ObjectId objectId) -> InterfaceType {
  ProxyInterface* proxy = this->proxy_from_object_id(objectId);
  if (proxy) {
    auto* context = dynamic_cast<typename InterfaceType::context_type*>(proxy);
    if (context) {
      return InterfaceType{context};
    } else {
      throw std::runtime_error("ProxyInterface is not of the requested InterfaceType");
    }
  } else {
    throw std::runtime_error("No proxy registered for given ObjectId");
  }
}

template <class InterfaceType>
  requires requires { typename InterfaceType::context_type; }
auto Connection::extract_arg_from_message(std::span<const char> buffer, InterfaceType arg)
    -> std::span<const char> {
  ObjectId objectId;
  std::span<const char> remainingBuffer = extract_arg_from_message(buffer, objectId);
  arg = from_object_id<InterfaceType>(objectId);
  return remainingBuffer;
}

template <class... Args>
auto Connection::send_message(ObjectId objectId, OpCode opCode, const Args&... args) -> void {
  std::uint16_t messageLength = 2 * sizeof(std::uint32_t); // header size
  ((messageLength += message_length(args)), ...);
  std::vector<char> storage(messageLength);
  std::span<char> buffer(storage);
  std::uint32_t lengthAndOpCode =
      (static_cast<std::uint32_t>(messageLength) << 16) | static_cast<std::uint16_t>(opCode);
  buffer = put_arg_to_message(buffer, objectId);
  buffer = put_arg_to_message(buffer, lengthAndOpCode);
  ((buffer = put_arg_to_message(buffer, args)), ...);
  auto set_fd = []<class Arg>(std::optional<FileDescriptorHandle>& fd, const Arg& arg) {
    if constexpr (std::same_as<std::remove_cvref_t<Arg>, FileDescriptorHandle>) {
      fd = arg;
    }
  };
  std::optional<FileDescriptorHandle> fd;
  (set_fd(fd, args), ...);
  send_message(std::move(storage), std::move(fd));
}

template <class InterfaceType>
  requires requires { typename InterfaceType::context_type; }
auto Connection::message_length(InterfaceType) -> std::uint16_t {
  return message_length(ObjectId{});
}

template <class InterfaceType>
  requires requires { typename InterfaceType::context_type; }
auto Connection::put_arg_to_message(std::span<char> buffer, InterfaceType arg) -> std::span<char> {
  ObjectId objectId = arg.get_object_id();
  return put_arg_to_message(buffer, objectId);
}

template <class... Args>
auto ProxyInterface::send_message(OpCode opCode, const Args&... args) -> void {
  mHandle.send_message(mObjectId, opCode, args...);
}

template <class... Args>
auto ProxyInterface::read_message(std::span<const char> buffer, Args&... args) -> std::size_t {
  return mHandle.read_message(buffer, args...);
}

} // namespace cw::wayland