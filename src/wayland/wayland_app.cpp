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

using namespace ms;

auto create_memory_fd() -> wayland::FileDescriptorHandle {
  int fd = ::memfd_create("wayland-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd == -1) {
    throw std::system_error(errno, std::generic_category(), "Failed to create shm file");
  }
  return wayland::FileDescriptorHandle{fd};
}

auto allocate_memory_file(std::size_t size) -> wayland::FileDescriptorHandle {
  wayland::FileDescriptorHandle fd = create_memory_fd();
  if (::ftruncate(fd.native_handle(), static_cast<off_t>(size)) == -1) {
    throw std::system_error(errno, std::generic_category(), "Failed to truncate shm file");
  }
  return fd;
}

auto fill_shm_buffer(std::mdspan<uint32_t, std::dextents<std::size_t, 2>> buffer) -> void {
  for (std::size_t y = 0; y < buffer.extent(1); ++y) {
    for (std::size_t x = 0; x < buffer.extent(0); ++x) {
      buffer[x, y] = 0xff0000ff; // ARGB: opaque blue
    }
  }
}

struct ApplicationState {
  explicit ApplicationState(wayland::ConnectionHandle handle) : connectionHandle(handle) {
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
        static_cast<uint32_t*>(mapped) + (stride * height) / 4,
        std::dextents<std::size_t, 2>{width, height}};
    fill_shm_buffer(mShmBuffer1);
    fill_shm_buffer(mShmBuffer2);
  }

  wayland::ConnectionHandle connectionHandle;
  wayland::Display display;
  wayland::Registry registry;
  wayland::Compositor compositor;
  wayland::Surface surface;
  wayland::Shm shm;
  wayland::ShmPool shmPool;
  wayland::Buffer buffer1;
  wayland::Buffer buffer2;
  wayland::FileDescriptorHandle shmFd;
  std::span<char> shmData;
  std::mdspan<uint32_t, std::dextents<std::size_t, 2>> mShmBuffer1;
  std::mdspan<uint32_t, std::dextents<std::size_t, 2>> mShmBuffer2;
};

auto create_window(
    ApplicationState& app,
    AsyncUnorderedMapHandle<std::string, wayland::Registry::GlobalEvent> nameFromInterface)
    -> IoTask<void> {
  auto compositorEvent = co_await nameFromInterface.wait_for(wayland::Compositor::interface_name());
  auto shmEvent = co_await nameFromInterface.wait_for(wayland::Shm::interface_name());
  auto xdgEvent = co_await nameFromInterface.wait_for(wayland::XdgWmBase::interface_name());
  auto compositorObjectId = app.connectionHandle.get_next_object_id();
  app.compositor =
      co_await use_resource(wayland::Compositor::make(compositorObjectId, app.connectionHandle));
  app.registry.bind(compositorEvent.name, compositorEvent.interface, compositorEvent.version,
                    compositorObjectId);
  app.surface = co_await use_resource(app.compositor.create_surface());
  auto shmObjectId = app.connectionHandle.get_next_object_id();
  app.shm = co_await use_resource(wayland::Shm::make(shmObjectId, app.connectionHandle));
  app.registry.bind(shmEvent.name, shmEvent.interface, shmEvent.version, shmObjectId);
  app.shmPool = co_await use_resource(
      app.shm.create_pool(app.shmFd, static_cast<std::int32_t>(app.shmData.size())));
  app.buffer1 = co_await use_resource(app.shmPool.create_buffer(
      0, 1920, 1080, 1920 * 4, static_cast<uint32_t>(wayland::Shm::Format::xrgb8888)));

  wayland::XdgWmBase xdgWmBase = co_await use_resource(
      wayland::XdgWmBase::make(app.connectionHandle.get_next_object_id(), app.connectionHandle));
  app.registry.bind(xdgEvent.name, xdgEvent.interface, xdgEvent.version, xdgWmBase.get_object_id());
  wayland::XdgSurface xdgSurface = co_await use_resource(xdgWmBase.get_xdg_surface(app.surface));
  wayland::XdgToplevel xdgToplevel = co_await use_resource(xdgSurface.get_toplevel());
  AsyncScopeHandle scope = co_await use_resource(create_scope());
  AsyncQueueHandle eventQueue =
      co_await use_resource(AsyncQueue<wayland::XdgSurface::ConfigureEvent>::make());

  auto env = co_await read_env([](auto x) { return x; });
  auto handleConfigureEvents =
      xdgSurface.events().subscribe([&](auto eventTask) noexcept -> IoTask<void> {
        auto event = co_await std::move(eventTask);
        switch (event.index()) {
        case wayland::XdgSurface::ConfigureEvent::index: {
          const auto configureEvent = std::get<wayland::XdgSurface::ConfigureEvent>(event);
          co_await eventQueue.push(std::move(configureEvent));
          co_return;
        }
        }
      });
  auto handlePingEvents =
      xdgWmBase.events().subscribe([&](auto eventTask) noexcept -> IoTask<void> {
        auto event = co_await std::move(eventTask);
        switch (event.index()) {
        case wayland::XdgWmBase::PingEvent::index: {
          const auto pingEvent = std::get<wayland::XdgWmBase::PingEvent>(event);
          xdgWmBase.pong(pingEvent.serial);
          break;
        }
        }
      });
  scope.spawn(std::move(handleConfigureEvents), env);
  scope.spawn(std::move(handlePingEvents), env);

  xdgToplevel.set_title("MusicStreamer Wayland Window");
  xdgToplevel.set_app_id("musicstreamer_wayland_example");
  xdgToplevel.set_maximized();

  app.surface.attach(app.buffer1, 0, 0);
  app.surface.damage(0, 0, 1920, 1080);
  app.surface.commit();

  while (true) {
    wayland::XdgSurface::ConfigureEvent event = co_await eventQueue.pop();
    xdgSurface.ack_configure(event.serial);
  }
}

int main() {
  wayland::Connection connection;
  sync_wait(connection.run().subscribe([](auto handle) noexcept -> IoTask<void> {
    ApplicationState app{co_await std::move(handle)};
    app.display = co_await use_resource(
        wayland::Display::make(wayland::ObjectId::Display, app.connectionHandle));
    app.registry = co_await use_resource(app.display.get_registry());
    auto nameFromInterface = co_await use_resource(
        make_async_unordered_map<std::string, wayland::Registry::GlobalEvent>());
    auto handleEvents =
        app.registry.events().subscribe([&](auto eventTask) noexcept -> IoTask<void> {
          auto event = co_await std::move(eventTask);
          switch (event.index()) {
          case wayland::Registry::GlobalEvent::index: {
            const auto globalEvent = std::get<wayland::Registry::GlobalEvent>(event);
            Log::i("Global added: name={}, interface={}, version={}", globalEvent.name,
                   globalEvent.interface, globalEvent.version);
            co_await nameFromInterface.emplace(std::string{globalEvent.interface}, globalEvent);
            break;
          }
          case wayland::Registry::GlobalRemoveEvent::index: {
            const auto removeEvent = std::get<wayland::Registry::GlobalRemoveEvent>(event);
            Log::i("Global removed: name={}", removeEvent.name);
            break;
          }
          }
        });
    co_await when_any(std::move(handleEvents), create_window(app, nameFromInterface));
  }));
}