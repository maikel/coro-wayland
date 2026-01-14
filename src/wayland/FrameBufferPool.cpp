// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "wayland/FrameBufferPool.hpp"
#include "AsyncChannel.hpp"
#include "AsyncQueue.hpp"
#include "Strand.hpp"
#include "coro_guard.hpp"
#include "narrow.hpp"
#include "observables/first.hpp"
#include "when_all.hpp"
#include "when_stop_requested.hpp"

#include <algorithm>
#include <system_error>

#include <linux/memfd.h>
#include <sys/mman.h>
#include <unistd.h>

namespace cw {

struct FrameBufferPoolContext : ImmovableBase {
  static constexpr std::size_t kMinHeight = 480;
  static constexpr std::size_t kMinWidth = 640;

  Client mClient;
  protocol::Shm mShm;
  protocol::ShmPool mShmPool;
  FileDescriptor mShmPoolFd;
  std::size_t mWidth;
  std::size_t mHeight;
  std::optional<AsyncScope> mCurrentBufferScope;
  std::optional<std::stop_source> mCurrentBufferStopSource;
  std::span<std::uint32_t> mShmData;
  std::array<protocol::Buffer, 2> mBuffers;
  std::array<PixelsView, 2> mPixelViews;
  std::array<AsyncChannel<AvailableBuffer>, 2> mAvailableBuffers;
  std::size_t mNextBufferIdx{};

  auto get_env() const noexcept {
    struct Env {
      const FrameBufferPoolContext* mContext;

      auto query(get_scheduler_t) const noexcept -> IoScheduler {
        return mContext->mClient.connection().get_scheduler();
      }

      auto query(get_stop_token_t) const noexcept -> std::stop_token {
        if (mContext->mCurrentBufferStopSource) {
          return mContext->mCurrentBufferStopSource->get_token();
        } else {
          return {};
        }
      }
    };
    return Env{this};
  }

  explicit FrameBufferPoolContext(Client client)
      : mClient(std::move(client)),
        mShmPoolFd(::memfd_create("wayland-shm-pool", MFD_CLOEXEC | MFD_ALLOW_SEALING)) {
    if (mShmPoolFd.native_handle() == -1) {
      throw std::system_error(errno, std::generic_category(), "Failed to create shm pool fd");
    }
    mWidth = 1;
    mHeight = 1;
    std::size_t size = mWidth * mHeight * sizeof(std::uint32_t) * 2;
    if (::ftruncate(mShmPoolFd.native_handle(), narrow<off_t>(size)) == -1) {
      throw std::system_error(errno, std::generic_category(), "Failed to truncate shm pool fd");
    }
    void* mapped =
        ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, mShmPoolFd.native_handle(), 0);
    if (mapped == MAP_FAILED) {
      throw std::system_error(errno, std::generic_category(), "Failed to mmap shm pool fd");
    }
    mShmData =
        std::span<std::uint32_t>(static_cast<std::uint32_t*>(mapped), size / sizeof(std::uint32_t));
  }

  auto resize(Width width, Height height) -> IoTask<void> {
    // first close old buffers
    if (mCurrentBufferStopSource && mCurrentBufferScope) {
      mCurrentBufferStopSource->request_stop();
      co_await mCurrentBufferScope->close();
      mCurrentBufferScope.reset();
      mCurrentBufferStopSource.reset();
    }

    // resize shm pool and remap
    auto newWidth = std::max(static_cast<std::size_t>(width), kMinWidth);
    auto newHeight = std::max(static_cast<std::size_t>(height), kMinHeight);
    std::size_t newSize = newWidth * newHeight * sizeof(std::uint32_t) * 2;
    if (newSize > mShmData.size_bytes()) {
      if (::ftruncate(mShmPoolFd.native_handle(), narrow<off_t>(newSize)) == -1) {
        throw std::system_error(errno, std::generic_category(), "Failed to truncate shm pool fd");
      }
      void* mapped = ::mremap(mShmData.data(), mShmData.size_bytes(), newSize, MREMAP_MAYMOVE);
      if (mapped == MAP_FAILED) {
        throw std::system_error(errno, std::generic_category(), "Failed to mmap shm pool fd");
      }
      mShmData = std::span<std::uint32_t>(static_cast<std::uint32_t*>(mapped),
                                          newSize / sizeof(std::uint32_t));
      mShmPool.resize(narrow<int32_t>(newSize));
      std::fill(mShmData.begin(), mShmData.end(), 0xff000000);
    }
    mWidth = newWidth;
    mHeight = newHeight;
    mPixelViews[0] = PixelsView{mShmData.subspan(0, mWidth * mHeight),
                                std::dextents<std::size_t, 2>{mWidth, mHeight}};
    mPixelViews[1] = PixelsView{mShmData.subspan(mWidth * mHeight),
                                std::dextents<std::size_t, 2>{mWidth, mHeight}};

    // create new shm pool and buffers
    auto createBuffer1 =
        mShmPool.create_buffer(0, narrow<int32_t>(mWidth), narrow<int32_t>(mHeight),
                               narrow<int32_t>(mWidth * sizeof(std::uint32_t)),
                               std::to_underlying(protocol::Shm::Format::argb8888));

    auto createBuffer2 = mShmPool.create_buffer(
        narrow<int32_t>(mWidth * mHeight * sizeof(std::uint32_t)), narrow<int32_t>(mWidth),
        narrow<int32_t>(mHeight), narrow<int32_t>(mWidth * sizeof(std::uint32_t)),
        std::to_underlying(protocol::Shm::Format::argb8888));

    auto queue = co_await use_resource(AsyncQueue<int>::make());

    auto bufferSubscriber1 = [&](IoTask<protocol::Buffer> bufferTask) -> IoTask<void> {
      protocol::Buffer buffer = co_await std::move(bufferTask);
      mBuffers[0] = buffer;
      co_await queue.push(0);
      co_await when_stop_requested();
      buffer.destroy();
    };

    auto bufferSubscriber2 = [&](IoTask<protocol::Buffer> bufferTask) -> IoTask<void> {
      protocol::Buffer buffer = co_await std::move(bufferTask);
      mBuffers[1] = buffer;
      co_await queue.push(1);
      co_await when_stop_requested();
      buffer.destroy();
    };

    mCurrentBufferStopSource.emplace();
    mCurrentBufferScope.emplace();

    mCurrentBufferScope->spawn(std::move(createBuffer1).subscribe(bufferSubscriber1), get_env());
    mCurrentBufferScope->spawn(std::move(createBuffer2).subscribe(bufferSubscriber2), get_env());

    co_await when_all(queue.pop(), queue.pop());
  }

  auto available_buffer() -> IoTask<AvailableBuffer> {
    AvailableBuffer buffer =
        co_await observables::first(mAvailableBuffers[mNextBufferIdx].receive());
    mNextBufferIdx = (mNextBufferIdx + 1) & 1;
    co_return buffer;
  }
};

auto FrameBufferPool::make(Client client) -> Observable<FrameBufferPool> {
  struct FrameBufferPoolObservable {
    Client mClient;

    static auto do_subscribe(Client client,
                             std::function<auto(IoTask<FrameBufferPool>)->IoTask<void>> receiver)
        -> IoTask<void> {
      FrameBufferPoolContext context(client);
      context.mAvailableBuffers[0] = co_await use_resource(AsyncChannel<AvailableBuffer>::make());
      context.mAvailableBuffers[1] = co_await use_resource(AsyncChannel<AvailableBuffer>::make());
      context.mShm = co_await use_resource(client.bind<protocol::Shm>());
      context.mShmPool = co_await use_resource(context.mShm.create_pool(
          context.mShmPoolFd, narrow<int32_t>(context.mShmData.size_bytes())));
      co_await context.resize(Width{640}, Height{480});
      FrameBufferPool pool{context};
      auto cleanup = [](FrameBufferPool pool) -> IoTask<void> {
        if (pool.mContext->mCurrentBufferStopSource && pool.mContext->mCurrentBufferScope) {
          pool.mContext->mCurrentBufferStopSource->request_stop();
          co_await pool.mContext->mCurrentBufferScope->close();
        }
        co_return;
      }(pool);
      co_await [&](FrameBufferPool pool, auto receiver) -> IoTask<void> {
        co_await coro_guard(std::move(cleanup));
        co_await receiver(coro_just(pool));
      }(pool, std::move(receiver));
    }

    auto
    subscribe(std::function<auto(IoTask<FrameBufferPool>)->IoTask<void>> receiver) const noexcept
        -> IoTask<void> {
      return do_subscribe(mClient, std::move(receiver));
    }
  };
  return FrameBufferPoolObservable{client};
}

auto FrameBufferPool::resize(Width width, Height height) -> IoTask<void> {
  return mContext->resize(width, height);
}

auto FrameBufferPool::available_buffer() -> IoTask<AvailableBuffer> {
  return mContext->available_buffer();
}

} // namespace cw