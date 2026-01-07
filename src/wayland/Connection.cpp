// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "wayland/Connection.hpp"

#include "IoContext.hpp"
#include "just_stopped.hpp"
#include "queries.hpp"
#include "read_env.hpp"
#include "stopped_as_optional.hpp"
#include "when_any.hpp"

#include "Logging.hpp"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <span>
#include <stdexcept>
#include <system_error>

#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ms::wayland {

// If WAYLAND_DISPLAY is set, concat with XDG_RUNTIME_DIR to form the path to the Unix socket.
// Assume the socket name is wayland-0 and concat with XDG_RUNTIME_DIR to form the path to the Unix
// socket. Give up.
Connection::Connection() {
  const char* displayEnv = std::getenv("WAYLAND_DISPLAY");
  std::string socketPath;
  if (displayEnv) {
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    if (runtimeDir) {
      socketPath = std::string(runtimeDir) + "/" + std::string(displayEnv);
    } else {
      socketPath =
          std::string("/run/user/") + std::to_string(getuid()) + "/" + std::string(displayEnv);
    }
  } else {
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    if (runtimeDir) {
      socketPath = std::string(runtimeDir) + "/wayland-0";
    } else {
      socketPath = std::string("/run/user/") + std::to_string(getuid()) + "/wayland-0";
    }
  }
  Log::d("Connecting to Wayland socket at {}", socketPath);
  mFd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (mFd == -1) {
    throw std::system_error(errno, std::generic_category(), "Failed to create Wayland socket");
  }

  struct sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

  if (::connect(mFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
    if (errno != EAGAIN) {
      ::close(mFd);
      throw std::system_error(errno, std::generic_category(),
                              "Failed to connect to Wayland socket at " + socketPath);
    } else {
      Log::d("Wayland socket connection in progress");
    }
  } else {
    Log::d("Connected to Wayland socket at {}", socketPath);
  }
}

Connection::~Connection() {
  if (mFd != -1) {
    ::close(mFd);
  }
}

namespace {
void log_message(std::string_view prefix, std::span<const char> message, std::size_t columns = 1) {
  Log::d("Message data ({} bytes):", message.size());
  std::size_t i = 0;
  std::string line;
  while (i < message.size()) {
    line = std::format("{} {:04x}:", prefix, i);
    for (std::size_t col = 0; col < columns; ++col) {
      if (i + col * sizeof(std::uint32_t) < message.size()) {
        std::uint32_t word = 0;
        std::memcpy(
            &word, message.data() + i + col * sizeof(std::uint32_t),
            std::min(sizeof(std::uint32_t), message.size() - i + col * sizeof(std::uint32_t)));
        line += std::format(" {:08x}", word);
      } else {
        line += "         ";
      }
    }
    line += "   |   ";
    for (std::size_t col = 0; col < columns && i + col * sizeof(std::uint32_t) < message.size();
         ++col) {
      for (std::size_t j = i + col * sizeof(std::uint32_t);
           j < i + (col + 1) * sizeof(std::uint32_t) && j < message.size(); ++j) {
        if (std::isprint(static_cast<unsigned char>(message[j]))) {
          line += std::format("{}", static_cast<char>(message[j]));
        } else {
          line += ".";
        }
      }
      line += " ";
    }
    i += columns * sizeof(std::uint32_t);
    Log::d("{}", line);
    line.clear();
  }
}
} // namespace

class ConnectionObservable {
public:
  explicit ConnectionObservable(Connection* connection) noexcept : mConnection(connection) {}

  template <class Subscriber> auto subscribe(Subscriber subscriber) noexcept -> IoTask<void> {
    return [](Connection* connection, Subscriber subscriber) -> IoTask<void> {
      IoScheduler scheduler = co_await ms::read_env(ms::get_scheduler);
      co_await scheduler.poll(connection->get_fd(), POLLIN | POLLOUT | POLLERR);
      int error = 0;
      socklen_t errorLen = sizeof(error);
      if (::getsockopt(connection->get_fd(), SOL_SOCKET, SO_ERROR, &error, &errorLen) == -1 ||
          error != 0) {
        throw std::system_error(errno, std::generic_category(), "Wayland socket error");
      } else {
        Log::d("Socket has no errors, connection established.");
      }
      auto handle = [](Connection* connection, IoScheduler scheduler) -> IoTask<ConnectionHandle> {
        co_return ConnectionHandle(connection, scheduler);
      }(connection, scheduler);
      std::exception_ptr exception = nullptr;
      bool stopped = false;
      try {
        stopped = !(co_await ms::stopped_as_optional(
                        ms::when_any(subscriber(std::move(handle)), recv_messages(connection))))
                       .has_value();
        if (stopped) {
          Log::d("Wayland connection was stopped.");
        } else {
          Log::d("Wayland connection tasks completed.");
        }
      } catch (std::exception& e) {
        Log::e("Caught exception: {}", e.what());
        exception = std::current_exception();
      }
      Log::d("Closing connection to wayland server.");
      co_await connection->close();
      if (stopped) {
        co_await ms::just_stopped();
      }
      if (exception) {
        std::rethrow_exception(exception);
      }
    }(mConnection, std::move(subscriber));
  }

private:
  static constexpr std::size_t kMinMessageSize = 8;

  static auto recv_at_least(Connection* connection, size_t minBytes, std::span<char> buffer)
      -> IoTask<std::size_t> {
    char controlBuffer[256];
    std::span<char> controlBufferSpan(controlBuffer);
    IoScheduler scheduler = co_await ms::read_env(ms::get_scheduler);
    std::size_t totalBytesRead = 0;
    std::span<char> remainingBuffer = buffer;
    while (totalBytesRead < minBytes) {
      ::msghdr msg{};
      ::iovec iov{};
      iov.iov_base = remainingBuffer.data();
      iov.iov_len = remainingBuffer.size();
      msg.msg_iov = &iov;
      msg.msg_iovlen = 1;
      msg.msg_control = controlBufferSpan.data();
      msg.msg_controllen = controlBufferSpan.size();
      ssize_t rc = ::recvmsg(connection->get_fd(), &msg, 0);
      if (rc > 0) {
        std::size_t bytesRead = static_cast<std::size_t>(rc);
        totalBytesRead += bytesRead;
        remainingBuffer = remainingBuffer.subspan(bytesRead);
        // Process any received file descriptors
        for (::cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
             cmsg = CMSG_NXTHDR(&msg, cmsg)) {
          if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            std::size_t fdCount = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            int* fds = reinterpret_cast<int*>(CMSG_DATA(cmsg));
            for (std::size_t i = 0; i < fdCount; ++i) {
              int fd = fds[i];
              Log::d("Received file descriptor {} from Wayland socket", fd);
              connection->mReceivedFileDescriptors.emplace(fd);
            }
          } else {
            Log::d("Received unknown control message of level {} and type {}", cmsg->cmsg_level,
                   cmsg->cmsg_type);
          }
        }
      } else if (rc == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          co_await scheduler.poll(connection->get_fd(), POLLIN);
        } else {
          throw std::system_error(errno, std::generic_category(),
                                  "Failed to read from Wayland socket");
        }
      } else {
        assert(rc == 0);
        // EOF reached
        break;
      }
    }
    co_return totalBytesRead;
  }

  static auto recv_messages(Connection* connection) -> IoTask<void> {
    char buffer[4096];
    std::size_t bytesRead = co_await recv_at_least(connection, kMinMessageSize, buffer);
    Log::d("Received {} bytes from Wayland socket", bytesRead);
    while (true) {
      assert(bytesRead >= kMinMessageSize);
      std::uint32_t objectId{};
      std::uint32_t MessageLengthAndOpCode{};
      std::memcpy(&objectId, buffer, 4);
      std::memcpy(&MessageLengthAndOpCode, buffer + 4, 4);
      const std::uint16_t opCode = static_cast<std::uint16_t>(MessageLengthAndOpCode & 0xFFFF);
      const std::uint16_t messageLength = static_cast<std::uint16_t>(MessageLengthAndOpCode >> 16);
      Log::d("Received message for object ID {} with op code {} and length {}", objectId, opCode,
             messageLength);
      if (messageLength > bytesRead) {
        std::span<char> bufferSpan(buffer);
        bufferSpan = bufferSpan.subspan(bytesRead);
        std::size_t additionalBytesRead =
            co_await recv_at_least(connection, messageLength - bytesRead, bufferSpan);
        bytesRead += additionalBytesRead;
        Log::d("Received additional {} bytes and a total of {} bytes from Wayland socket",
               additionalBytesRead, bytesRead);
      }
      std::span<const char> message(buffer, messageLength);
      auto proxyIt = connection->mProxies.find(static_cast<ObjectId>(objectId));
      if (proxyIt != connection->mProxies.end() && proxyIt->second) {
        ProxyInterface* proxy = proxyIt->second;
        Log::d("Dispatching message to proxy for object ID {}", objectId);
        log_message("S->C", message, 4);
        co_await proxy->handle_message(message, OpCode{opCode});
        std::rotate(buffer, buffer + messageLength, buffer + bytesRead);
        bytesRead -= messageLength;
        if (bytesRead < kMinMessageSize) {
          std::size_t additionalBytesRead = co_await recv_at_least(
              connection, kMinMessageSize - bytesRead,
              std::span<char>(buffer + bytesRead, sizeof(buffer) - bytesRead));
          bytesRead += additionalBytesRead;
          Log::d("Received additional {} bytes and a total of {} bytes from Wayland socket",
                 additionalBytesRead, bytesRead);
        }
      } else {
        Log::d("No proxy registered for object ID {}, ignoring message", objectId);
      }
    }
  }

  Connection* mConnection;
};

auto Connection::run() -> Observable<ConnectionHandle> {
  return Observable<ConnectionHandle>{ConnectionObservable{this}};
}

auto Connection::close() -> Task<void> { co_await mScope.close(); }

auto ConnectionHandle::register_interface(ProxyInterface* proxy) -> void {
  mConnection->mProxies[proxy->get_object_id()] = proxy;
}

auto ConnectionHandle::read_next_file_descriptor() -> FileDescriptorHandle {
  if (mConnection->mReceivedFileDescriptors.empty()) {
    throw std::runtime_error("No received file descriptors available");
  }
  FileDescriptorHandle fdHandle = mConnection->mReceivedFileDescriptors.front();
  mConnection->mReceivedFileDescriptors.pop();
  return fdHandle;
}

FileDescriptorHandle::FileDescriptorHandle(int handle) : mNativeHandle(handle) {}

auto FileDescriptorHandle::native_handle() const noexcept -> int { return mNativeHandle; }

auto ConnectionHandle::extract_arg_from_message(std::span<const char> buffer, std::string& arg)
    -> std::span<const char> {
  if (buffer.size() < sizeof(std::uint32_t)) {
    throw std::runtime_error("Buffer too small to extract array argument");
  }
  std::uint32_t length = 0;
  std::memcpy(&length, buffer.data(), sizeof(std::uint32_t));
  buffer = buffer.subspan(sizeof(std::uint32_t));
  if (buffer.size() < length) {
    throw std::runtime_error("Buffer too small to extract array argument data");
  }
  arg.assign_range(buffer.subspan(0, length - 1)); // exclude null terminator
  constexpr unsigned n = sizeof(std::uint32_t) - 1;
  std::size_t paddedLength = (length + n) & ~n;
  return buffer.subspan(paddedLength);
}

auto ConnectionHandle::extract_arg_from_message(std::span<const char> buffer,
                                                std::vector<char>& arg) -> std::span<const char> {
  if (buffer.size() < sizeof(std::uint32_t)) {
    throw std::runtime_error("Buffer too small to extract array argument");
  }
  std::uint32_t length = 0;
  std::memcpy(&length, buffer.data(), sizeof(std::uint32_t));
  buffer = buffer.subspan(sizeof(std::uint32_t));
  if (buffer.size() < length) {
    throw std::runtime_error("Buffer too small to extract array argument data");
  }
  arg.assign_range(buffer.subspan(0, length));
  constexpr unsigned n = sizeof(std::uint32_t) - 1;
  std::size_t paddedLength = (length + n) & ~n;
  return buffer.subspan(paddedLength);
}

auto ConnectionHandle::extract_arg_from_message(std::span<const char> buffer, std::int32_t& arg)
    -> std::span<const char> {
  if (buffer.size() < sizeof(std::int32_t)) {
    throw std::runtime_error("Buffer too small to extract int32 argument");
  }
  std::memcpy(&arg, buffer.data(), sizeof(std::uint32_t));
  return buffer.subspan(sizeof(std::uint32_t));
}

auto ConnectionHandle::extract_arg_from_message(std::span<const char> buffer, std::uint32_t& arg)
    -> std::span<const char> {
  if (buffer.size() < sizeof(std::uint32_t)) {
    throw std::runtime_error("Buffer too small to extract uint32 argument");
  }
  std::memcpy(&arg, buffer.data(), sizeof(std::uint32_t));
  return buffer.subspan(sizeof(std::uint32_t));
}

auto ConnectionHandle::extract_arg_from_message(std::span<const char> buffer, ObjectId& arg)
    -> std::span<const char> {
  if (buffer.size() < sizeof(std::uint32_t)) {
    throw std::runtime_error("Buffer too small to extract ObjectId argument");
  }
  std::uint32_t rawId = 0;
  std::memcpy(&rawId, buffer.data(), sizeof(std::uint32_t));
  arg = static_cast<ObjectId>(rawId);
  return buffer.subspan(sizeof(std::uint32_t));
}

auto ConnectionHandle::extract_arg_from_message(std::span<const char> buffer,
                                                FileDescriptorHandle& arg)
    -> std::span<const char> {
  arg = read_next_file_descriptor();
  return buffer;
}

auto ConnectionHandle::message_length(const std::string& arg) -> std::uint16_t {
  constexpr unsigned n = sizeof(std::uint32_t) - 1;
  std::size_t paddedLength = (arg.size() + 1 + n) & ~n; // include null terminator
  return static_cast<std::uint16_t>(sizeof(std::uint32_t) + paddedLength);
}

auto ConnectionHandle::message_length(std::span<const char> arg) -> std::uint16_t {
  constexpr unsigned n = sizeof(std::uint32_t) - 1;
  std::size_t paddedLength = (arg.size() + n) & ~n;
  return static_cast<std::uint16_t>(sizeof(std::uint32_t) + paddedLength);
}

auto ConnectionHandle::message_length(std::int32_t) -> std::uint16_t {
  return sizeof(std::int32_t);
}

auto ConnectionHandle::message_length(std::uint32_t) -> std::uint16_t {
  return sizeof(std::uint32_t);
}

auto ConnectionHandle::message_length(ObjectId) -> std::uint16_t { return sizeof(std::uint32_t); }

auto ConnectionHandle::message_length(FileDescriptorHandle) -> std::uint16_t { return 0; }

auto ConnectionHandle::message_length(const ProxyInterface*) -> std::uint16_t {
  return sizeof(std::uint32_t);
}

auto ConnectionHandle::put_arg_to_message(std::span<char> buffer, const std::string& arg)
    -> std::span<char> {
  std::span<const char> strContent(arg.data(), arg.size() + 1); // include null terminator
  return put_arg_to_message(buffer, strContent);
}

auto ConnectionHandle::put_arg_to_message(std::span<char> buffer, std::span<const char> arg)
    -> std::span<char> {
  if (buffer.size() < sizeof(std::uint32_t) + arg.size()) {
    throw std::runtime_error("Buffer too small to put array argument");
  }
  std::uint32_t length = static_cast<std::uint32_t>(arg.size());
  std::memcpy(buffer.data(), &length, sizeof(std::uint32_t));
  buffer = buffer.subspan(sizeof(std::uint32_t));
  std::memcpy(buffer.data(), arg.data(), arg.size());
  buffer = buffer.subspan(arg.size());
  constexpr unsigned n = sizeof(std::uint32_t) - 1;
  std::size_t paddedLength = (length + n) & ~n;
  std::size_t padding = paddedLength - length;
  if (padding > 0) {
    std::memset(buffer.data(), 0, padding);
    buffer = buffer.subspan(padding);
  }
  return buffer;
}

auto ConnectionHandle::put_arg_to_message(std::span<char> buffer, std::int32_t arg)
    -> std::span<char> {
  if (buffer.size() < sizeof(std::int32_t)) {
    throw std::runtime_error("Buffer too small to put int32 argument");
  }
  std::memcpy(buffer.data(), &arg, sizeof(std::int32_t));
  return buffer.subspan(sizeof(std::int32_t));
}

auto ConnectionHandle::put_arg_to_message(std::span<char> buffer, std::uint32_t arg)
    -> std::span<char> {
  if (buffer.size() < sizeof(std::uint32_t)) {
    throw std::runtime_error("Buffer too small to put uint32 argument");
  }
  std::memcpy(buffer.data(), &arg, sizeof(std::uint32_t));
  return buffer.subspan(sizeof(std::uint32_t));
}

auto ConnectionHandle::put_arg_to_message(std::span<char> buffer, ObjectId arg) -> std::span<char> {
  if (buffer.size() < sizeof(std::uint32_t)) {
    throw std::runtime_error("Buffer too small to put ObjectId argument");
  }
  std::uint32_t rawId = static_cast<std::uint32_t>(arg);
  std::memcpy(buffer.data(), &rawId, sizeof(std::uint32_t));
  return buffer.subspan(sizeof(std::uint32_t));
}

auto ConnectionHandle::put_arg_to_message(std::span<char> buffer, FileDescriptorHandle)
    -> std::span<char> {
  return buffer;
}

auto ConnectionHandle::put_arg_to_message(std::span<char> buffer, const ProxyInterface* proxy)
    -> std::span<char> {
  ObjectId objectId = proxy->get_object_id();
  return put_arg_to_message(buffer, objectId);
}

auto ConnectionHandle::get_next_object_id() -> ObjectId {
  return static_cast<ObjectId>(mConnection->mNextObjectId.fetch_add(1));
}

auto ConnectionHandle::unregister_interface(ProxyInterface* proxy) -> void {
  mConnection->mProxies.at(proxy->get_object_id()) = nullptr;
}

void ConnectionHandle::send_message(std::vector<char> message,
                                    std::optional<FileDescriptorHandle> fds) {
  auto task = [](ConnectionHandle self, std::vector<char> message,
                 std::optional<FileDescriptorHandle> fds) -> Task<void> {
    char controlBuffer[sizeof(::cmsghdr) + sizeof(int)];
    std::span<char> controlBufferSpan(controlBuffer);
    ::msghdr msg{};
    ::iovec iov{};
    iov.iov_base = message.data();
    iov.iov_len = message.size();
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (fds.has_value()) {
      msg.msg_control = controlBufferSpan.data();
      msg.msg_controllen = controlBufferSpan.size();
      ::cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
      cmsg->cmsg_level = SOL_SOCKET;
      cmsg->cmsg_type = SCM_RIGHTS;
      cmsg->cmsg_len = CMSG_LEN(sizeof(int));
      int* fdData = reinterpret_cast<int*>(CMSG_DATA(cmsg));
      fdData[0] = fds->native_handle();
      msg.msg_controllen = cmsg->cmsg_len;
    }
    while (true) {
      ssize_t rc = ::sendmsg(self.mConnection->get_fd(), &msg, 0);
      int ec = errno;
      if (rc == -1 && (ec != EAGAIN && ec != EWOULDBLOCK)) {
        throw std::system_error(ec, std::generic_category(),
                                "Failed to send message to Wayland socket");
      } else if (rc != -1) {
        Log::d("Sent {} bytes to Wayland socket", rc);
        log_message("C->S", std::span<const char>(message.data(), message.size()), 4);
        break;
      } else {
        Log::d("Wayland socket send would block, message not sent");
        co_await self.mScheduler.poll(self.mConnection->get_fd(), POLLOUT);
      }
    }
  }(*this, std::move(message), fds);
  mConnection->mScope.spawn(std::move(task));
}

} // namespace ms::wayland