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
      auto handle = [](Connection* connection) -> IoTask<ConnectionHandle> {
        co_return ConnectionHandle(connection);
      }(connection);
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

  struct RecvMessageResult {
    std::size_t bytesRead;
    std::size_t controlBytesRead;
  };

  static auto recv_at_least(Connection* connection, size_t minBytes, std::span<char> buffer,
                            std::span<char> controlBuffer) -> IoTask<RecvMessageResult> {
    IoScheduler scheduler = co_await ms::read_env(ms::get_scheduler);
    std::size_t totalBytesRead = 0;
    std::size_t totalControlBytesRead = 0;
    std::span<char> remainingBuffer = buffer;
    std::span<char> remainingControlBuffer = controlBuffer;
    while (totalBytesRead < minBytes) {
      ::msghdr msg{};
      ::iovec iov{};
      iov.iov_base = remainingBuffer.data();
      iov.iov_len = remainingBuffer.size();
      msg.msg_iov = &iov;
      msg.msg_iovlen = 1;
      msg.msg_control = remainingControlBuffer.data();
      msg.msg_controllen = remainingControlBuffer.size();
      ssize_t rc = ::recvmsg(connection->get_fd(), &msg, 0);
      if (rc > 0) {
        std::size_t bytesRead = static_cast<std::size_t>(rc);
        totalBytesRead += bytesRead;
        totalControlBytesRead += msg.msg_controllen;
        remainingBuffer = remainingBuffer.subspan(bytesRead);
        remainingControlBuffer = remainingControlBuffer.subspan(msg.msg_controllen);
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
    RecvMessageResult result{};
    result.bytesRead = totalBytesRead;
    result.controlBytesRead = totalControlBytesRead;
    co_return result;
  }

  static auto recv_messages(Connection* connection) -> IoTask<void> {
    while (true) {
      char buffer[4096];
      char controlBuffer[256];
      auto [bytesRead, controlBytesRead] =
          co_await recv_at_least(connection, kMinMessageSize, buffer, controlBuffer);
      Log::d("Received {} data bytes and {} control bytes from Wayland socket", bytesRead,
             controlBytesRead);
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
        std::span<char> controlBufferSpan(controlBuffer);
        bufferSpan = bufferSpan.subspan(bytesRead);
        controlBufferSpan = controlBufferSpan.subspan(controlBytesRead);
        auto [additionalBytesRead, additionalControlBytesRead] = co_await recv_at_least(
            connection, messageLength - bytesRead, bufferSpan, controlBufferSpan);
        bytesRead += additionalBytesRead;
        controlBytesRead += additionalControlBytesRead;
        Log::d("Received additional {} data bytes and {} control bytes from Wayland socket",
               additionalBytesRead, additionalControlBytesRead);
        Log::d(
            "Finally received {} data bytes and {} control bytes for the complete message header",
            messageLength, controlBytesRead);
      }
      std::span<const char> message(buffer, messageLength);
      auto proxyIt = connection->mProxies.find(static_cast<ObjectId>(objectId));
      if (proxyIt != connection->mProxies.end()) {
        ProxyInterface* proxy = proxyIt->second;
        Log::d("Dispatching message to proxy for object ID {}", objectId);
        co_await proxy->handle_message(message);
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

} // namespace ms::wayland