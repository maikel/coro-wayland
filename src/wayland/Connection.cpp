// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "wayland/Connection.hpp"

#include "IoContext.hpp"
#include "queries.hpp"
#include "read_env.hpp"
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

class ConnectionObserver {
public:
  explicit ConnectionObserver(Connection* connection) noexcept : mConnection(connection) {}

  template <class Receiver> auto subscribe(Receiver receiver) noexcept -> IoTask<void> {
    return [](Connection* connection, Receiver receiver) -> IoTask<void> {
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
      try {
        co_await ms::when_any(receiver(std::move(handle)), readMessages(connection));
      } catch (...) {
        exception = std::current_exception();
      }
      co_await connection->close();
      if (exception) {
        std::rethrow_exception(exception);
      }
    }(mConnection, std::move(receiver));
  }

private:
  static constexpr std::size_t kMinMessageSize = 8;

  static auto read_at_least(Connection* connection, size_t minBytes, std::span<char> buffer)
      -> IoTask<size_t> {
    IoScheduler scheduler = co_await ms::read_env(ms::get_scheduler);
    size_t totalBytesRead = 0;
    std::span<char> remainingBuffer = buffer;
    while (totalBytesRead < minBytes) {
      ssize_t bytesRead =
          ::read(connection->get_fd(), remainingBuffer.data(),
                 remainingBuffer.size());
      if (bytesRead > 0) {
        totalBytesRead += static_cast<size_t>(bytesRead);
        remainingBuffer = remainingBuffer.subspan(static_cast<size_t>(bytesRead));
      } else if (bytesRead == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          co_await scheduler.poll(connection->get_fd(), POLLIN);
        } else {
          throw std::system_error(errno, std::generic_category(),
                                  "Failed to read from Wayland socket");
        }
      } else {
        assert(bytesRead == 0);
        // EOF reached
        break;
      }
    }
    co_return totalBytesRead;
  }

  static auto readMessages(Connection* connection) -> IoTask<void> {
    while (true) {
      char buffer[4096];
      size_t bytesRead = co_await read_at_least(connection, kMinMessageSize, buffer);
      Log::d("Read {} bytes from Wayland socket", bytesRead);
      assert(bytesRead >= kMinMessageSize);
      std::uint32_t objectId{};
      std::uint32_t MessageLengthAndOpCode{};
      std::memcpy(&objectId, buffer, 4);
      std::memcpy(&MessageLengthAndOpCode, buffer + 4, 4);
      const std::uint16_t opCode = static_cast<std::uint16_t>(MessageLengthAndOpCode & 0xFFFF);
      const std::uint16_t messageLength = static_cast<std::uint16_t>(MessageLengthAndOpCode >> 16);
      Log::d("Received message for object ID {} with op code {} and length {}", objectId,
             opCode, messageLength);
      if (messageLength > bytesRead) {
        size_t additionalBytesRead =
            co_await read_at_least(connection, messageLength - bytesRead,
                                   std::span<char>(buffer + bytesRead, sizeof(buffer) - bytesRead));
        bytesRead += additionalBytesRead;
        Log::d("Read additional {} bytes from Wayland socket", additionalBytesRead);
      }
      std::span<const char> message(buffer, messageLength);
      auto proxyIt = connection->mProxies.find(static_cast<ObjectId>(objectId));
      if (proxyIt != connection->mProxies.end()) {
        ProxyBase* proxy = proxyIt->second;
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
  return Observable<ConnectionHandle>{ConnectionObserver{this}};
}

auto Connection::close() -> Task<void> { co_await mScope.close(); }

} // namespace ms::wayland