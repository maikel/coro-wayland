// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "AsyncUnorderedMap.hpp"
#include "Logging.hpp"
#include "observables/use_resource.hpp"
#include "queries.hpp"
#include "read_env.hpp"
#include "sync_wait.hpp"
#include "wayland/Connection.hpp"
#include "wayland/XdgShell.hpp"
#include "wayland/protocol.hpp"
#include "when_all.hpp"
#include "when_any.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mdspan>
#include <span>

using namespace cw;

auto create_memory_fd() -> FileDescriptor {
  int fd = ::memfd_create("wayland-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd == -1) {
    throw std::system_error(errno, std::generic_category(), "Failed to create shm file");
  }
  return FileDescriptor{fd};
}

auto allocate_memory_file(std::size_t size) -> FileDescriptor {
  FileDescriptor fd = create_memory_fd();
  if (::ftruncate(fd.native_handle(), static_cast<off_t>(size)) == -1) {
    throw std::system_error(errno, std::generic_category(), "Failed to truncate shm file");
  }
  return fd;
}

auto fill_shm_buffer(std::mdspan<uint32_t, std::dextents<std::size_t, 2>> buffer,
                     std::uint32_t color) -> void {
  for (std::size_t y = 0; y < buffer.extent(1); ++y) {
    for (std::size_t x = 0; x < buffer.extent(0); ++x) {
      buffer[x, y] = color;
    }
  }
}

struct ApplicationState {
  explicit ApplicationState(Connection conn) : connection(conn) {
    const int width = 1920, height = 1080;
    const int stride = width * 4;
    const int shm_pool_size = height * stride * 2;
    shmFd = allocate_memory_file(shm_pool_size);
    void* mapped = ::mmap(nullptr, shm_pool_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                          shmFd.native_handle(), 0);
    if (mapped == MAP_FAILED) {
      throw std::system_error(errno, std::generic_category(), "Failed to mmap shm file");
    }
    shmData = std::span<char>{static_cast<char*>(mapped), shm_pool_size};
    mShmBuffer1 = std::mdspan<uint32_t, std::dextents<std::size_t, 2>>{
        static_cast<uint32_t*>(mapped), std::dextents<std::size_t, 2>{width, height}};
    mShmBuffer2 = std::mdspan<uint32_t, std::dextents<std::size_t, 2>>{
        static_cast<uint32_t*>(mapped) + mShmBuffer1.size(),
        std::dextents<std::size_t, 2>{width, height}};
    fill_shm_buffer(mShmBuffer1, 0xff0000ff);
    fill_shm_buffer(mShmBuffer2, 0xff00ff00);
  }

  Connection connection;
  protocol::Display display;
  protocol::Registry registry;
  protocol::Compositor compositor;
  protocol::Surface surface;
  protocol::Shm shm;
  protocol::ShmPool shmPool;
  protocol::Buffer buffer1;
  protocol::Buffer buffer2;
  FileDescriptor shmFd;
  std::span<char> shmData;
  std::mdspan<uint32_t, std::dextents<std::size_t, 2>> mShmBuffer1;
  std::mdspan<uint32_t, std::dextents<std::size_t, 2>> mShmBuffer2;
};

auto create_window(
    ApplicationState& app,
    AsyncUnorderedMapHandle<std::string, protocol::Registry::GlobalEvent> nameFromInterface)
    -> IoTask<void> {
  auto compositorEvent =
      co_await nameFromInterface.wait_for(protocol::Compositor::interface_name());
  auto shmEvent = co_await nameFromInterface.wait_for(protocol::Shm::interface_name());
  auto xdgEvent = co_await nameFromInterface.wait_for(protocol::XdgWmBase::interface_name());
  auto compositorObjectId = app.connection.get_next_object_id();
  app.compositor =
      co_await use_resource(protocol::Compositor::make(compositorObjectId, app.connection));
  app.registry.bind(compositorEvent.name, compositorEvent.interface, compositorEvent.version,
                    compositorObjectId);
  app.surface = co_await use_resource(app.compositor.create_surface());
  auto shmObjectId = app.connection.get_next_object_id();
  app.shm = co_await use_resource(protocol::Shm::make(shmObjectId, app.connection));
  app.registry.bind(shmEvent.name, shmEvent.interface, shmEvent.version, shmObjectId);
  app.shmPool = co_await use_resource(
      app.shm.create_pool(app.shmFd, static_cast<std::int32_t>(app.shmData.size())));
  app.buffer1 = co_await use_resource(app.shmPool.create_buffer(
      0, 1920, 1080, 1920 * 4, static_cast<uint32_t>(protocol::Shm::Format::xrgb8888)));
  app.buffer2 = co_await use_resource(
      app.shmPool.create_buffer(1920 * 1080 * 4, 1920, 1080, 1920 * 4,
                                static_cast<uint32_t>(protocol::Shm::Format::xrgb8888)));

  protocol::XdgWmBase xdgWmBase = co_await use_resource(
      protocol::XdgWmBase::make(app.connection.get_next_object_id(), app.connection));
  app.registry.bind(xdgEvent.name, xdgEvent.interface, xdgEvent.version, xdgWmBase.get_object_id());
  protocol::XdgSurface xdgSurface = co_await use_resource(xdgWmBase.get_xdg_surface(app.surface));
  protocol::XdgToplevel xdgToplevel = co_await use_resource(xdgSurface.get_toplevel());
  AsyncScopeHandle scope = co_await use_resource(create_scope());

  auto env = co_await read_env([](auto x) { return x; });
  auto handleConfigureEvents =
      xdgSurface.events().subscribe([&](auto eventTask) noexcept -> IoTask<void> {
        auto event = co_await std::move(eventTask);
        switch (event.index()) {
        case protocol::XdgSurface::ConfigureEvent::index: {
          const auto configureEvent = std::get<protocol::XdgSurface::ConfigureEvent>(event);
          Log::i("Received configure event with serial {}", configureEvent.serial);
          xdgSurface.ack_configure(configureEvent.serial);
          co_return;
        }
        }
      });
  auto handlePingEvents =
      xdgWmBase.events().subscribe([&](auto eventTask) noexcept -> IoTask<void> {
        auto event = co_await std::move(eventTask);
        switch (event.index()) {
        case protocol::XdgWmBase::PingEvent::index: {
          const auto pingEvent = std::get<protocol::XdgWmBase::PingEvent>(event);
          xdgWmBase.pong(pingEvent.serial);
          break;
        }
        }
      });
  scope.spawn(std::move(handleConfigureEvents), env);
  scope.spawn(std::move(handlePingEvents), env);

  xdgToplevel.set_title("CoroWayland Wayland Window");
  xdgToplevel.set_app_id("CoroWayland_wayland_example");
  xdgToplevel.set_maximized();

  app.surface.attach(app.buffer1, 0, 0);
  app.surface.damage(0, 0, 1920, 1080);
  app.surface.commit();

  int activeBuffer = 0;
  protocol::Buffer buffers[2]{app.buffer1, app.buffer2};
  while (true) {
    co_await app.connection.get_scheduler().schedule_after(std::chrono::seconds(1));
    Log::i("Swapping buffer to display color {}", activeBuffer == 0 ? "green" : "blue");
    activeBuffer = (activeBuffer + 1) % 2;
    app.surface.attach(buffers[activeBuffer], 0, 0);
    app.surface.damage(0, 0, 1920, 1080);
    app.surface.commit();
  }
}

auto coro_main() -> IoTask<void> {
  Connection connection = co_await use_resource(Connection::make());
  ApplicationState app{connection};
  app.display = co_await use_resource(protocol::Display::make(ObjectId::Display, app.connection));
  app.registry = co_await use_resource(app.display.get_registry());
  auto nameFromInterface = co_await use_resource(
      make_async_unordered_map<std::string, protocol::Registry::GlobalEvent>());
  auto handleEvents = app.registry.events().subscribe([&](auto eventTask) noexcept -> IoTask<void> {
    auto event = co_await std::move(eventTask);
    switch (event.index()) {
    case protocol::Registry::GlobalEvent::index: {
      const auto globalEvent = std::get<protocol::Registry::GlobalEvent>(event);
      Log::i("Global added: name={}, interface={}, version={}", globalEvent.name,
             globalEvent.interface, globalEvent.version);
      co_await nameFromInterface.emplace(globalEvent.interface, globalEvent);
      break;
    }
    case protocol::Registry::GlobalRemoveEvent::index: {
      const auto removeEvent = std::get<protocol::Registry::GlobalRemoveEvent>(event);
      Log::i("Global removed: name={}", removeEvent.name);
      break;
    }
    }
  });
  co_await when_any(std::move(handleEvents), create_window(app, nameFromInterface));
}

int main() { sync_wait(coro_main()); }