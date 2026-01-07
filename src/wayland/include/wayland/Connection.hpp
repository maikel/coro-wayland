// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "AsyncScope.hpp"
#include "Observable.hpp"

#include <cstdint>
#include <queue>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/socket.h>

namespace ms::wayland {

struct FileDescriptorHandle {
public:
  FileDescriptorHandle() noexcept : mNativeHandle(-1) {}

  explicit FileDescriptorHandle(int handle);

  auto native_handle() const noexcept -> int;

  friend auto operator<=>(FileDescriptorHandle, FileDescriptorHandle) = default;

private:
  int mNativeHandle;
};

enum class ObjectId : std::uint32_t {
  Display = 1,
};

enum class OpCode : std::uint16_t {};

class ProxyInterface;
class Connection;

class ConnectionHandle {
public:
  explicit ConnectionHandle(Connection* connection, IoScheduler scheduler) noexcept
      : mConnection(connection), mScheduler(scheduler) {}

  template <class... Args>
  auto send_message(ObjectId objectId, OpCode opCode, const Args&... args) -> void;

  template <class... Args>
  auto read_message(std::span<const char> buffer, Args&... args) -> std::size_t;

  auto read_next_file_descriptor() -> FileDescriptorHandle;

  template <class InterfaceType>
    requires requires { typename InterfaceType::context_type; }
  auto from_object_id(ObjectId objectId) -> InterfaceType;

  auto get_next_object_id() -> ObjectId;

  auto unregister_interface(ProxyInterface* proxy) -> void;

  auto get_scheduler() const noexcept -> IoScheduler { return mScheduler; }

private:
  friend class ProxyInterface;
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

  Connection* mConnection;
  IoScheduler mScheduler;
};

class ProxyInterface : ImmovableBase {
public:
  explicit ProxyInterface(ObjectId objectId, ConnectionHandle handle) noexcept
      : mObjectId(objectId), mHandle(handle) {
    mHandle.register_interface(this);
  }

  virtual ~ProxyInterface() { mHandle.unregister_interface(this); }

  virtual auto handle_message(std::span<const char> message, OpCode code) -> IoTask<void> = 0;

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
  std::atomic<std::uint32_t> mNextObjectId{2};
  int mFd;
  std::unordered_map<ObjectId, ProxyInterface*> mProxies;
  std::queue<FileDescriptorHandle> mReceivedFileDescriptors;
};

template <class... Args>
auto ConnectionHandle::read_message(std::span<const char> buffer, Args&... args) -> std::size_t {
  std::span<const char> remainingBuffer = buffer.subspan(2 * sizeof(std::uint32_t)); // skip header
  ((remainingBuffer = extract_arg_from_message(remainingBuffer, args)), ...);
  return buffer.size() - remainingBuffer.size();
}

template <class InterfaceType>
  requires requires { typename InterfaceType::context_type; }
auto ConnectionHandle::from_object_id(ObjectId objectId) -> InterfaceType {
  auto it = mConnection->mProxies.find(objectId);
  if (it != mConnection->mProxies.end()) {
    auto* context = dynamic_cast<typename InterfaceType::context_type*>(it->second);
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
auto ConnectionHandle::extract_arg_from_message(std::span<const char> buffer, InterfaceType arg)
    -> std::span<const char> {
  ObjectId objectId;
  std::span<const char> remainingBuffer = extract_arg_from_message(buffer, objectId);
  arg = from_object_id<InterfaceType>(objectId);
  return remainingBuffer;
}

template <class... Args>
auto ConnectionHandle::send_message(ObjectId objectId, OpCode opCode, const Args&... args) -> void {
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
auto ConnectionHandle::message_length(InterfaceType) -> std::uint16_t {
  return message_length(ObjectId{});
}

template <class InterfaceType>
  requires requires { typename InterfaceType::context_type; }
auto ConnectionHandle::put_arg_to_message(std::span<char> buffer, InterfaceType arg)
    -> std::span<char> {
  ObjectId objectId = arg.get_object_id();
  return put_arg_to_message(buffer, objectId);
}

} // namespace ms::wayland