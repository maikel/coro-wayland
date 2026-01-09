// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "wayland/FrameBufferPool.hpp"
#include "AsyncQueue.hpp"
#include "Strand.hpp"
#include "narrow.hpp"
#include "when_all.hpp"
#include "when_stop_requested.hpp"

#include "Logging.hpp"

#include <system_error>

#include <linux/memfd.h>
#include <sys/mman.h>
#include <unistd.h>

namespace cw::protocol {

struct FrameBufferPoolContext : ImmovableBase {
  Client mClient;
  Strand mStrand;
  Shm mShm;
  ShmPool mShmPool;
  FileDescriptor mShmPoolFd;
  std::size_t mWidth;
  std::size_t mHeight;
  std::optional<AsyncScope> mCurrentBufferScope;
  std::optional<std::stop_source> mCurrentBufferStopSource;
  std::span<std::uint32_t> mShmData;
  std::array<Buffer, 2> mBuffers;
  std::array<std::mdspan<std::uint32_t, std::dextents<std::size_t, 2>>, 2> mPixelViews;

  auto get_env() const noexcept {
    struct Env {
      const FrameBufferPoolContext* mContext;

      auto query(get_scheduler_t) const noexcept -> IoScheduler {
        return mContext->mStrand.get_scheduler();
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

  explicit FrameBufferPoolContext(Client client, Strand strand)
      : mClient(std::move(client)), mStrand(std::move(strand)),
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
    co_await use_resource(mStrand.lock());

    // first close old buffers
    if (mCurrentBufferStopSource && mCurrentBufferScope) {
      mCurrentBufferStopSource->request_stop();
      co_await mCurrentBufferScope->close();
      mCurrentBufferScope.reset();
      mCurrentBufferStopSource.reset();
    }

    // resize shm pool and remap
    auto newWidth = static_cast<std::size_t>(width);
    auto newHeight = static_cast<std::size_t>(height);
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
    }
    mWidth = newWidth;
    mHeight = newHeight;
    mPixelViews[0] = std::mdspan<std::uint32_t, std::dextents<std::size_t, 2>>{
        mShmData.data(), std::dextents<std::size_t, 2>{mWidth, mHeight}};
    mPixelViews[1] = std::mdspan<std::uint32_t, std::dextents<std::size_t, 2>>{
        mShmData.data() + mWidth * mHeight, std::dextents<std::size_t, 2>{mWidth, mHeight}};

    // create new shm pool and buffers
    auto createBuffer1 = mShmPool.create_buffer(
        0, narrow<int32_t>(mWidth), narrow<int32_t>(mHeight),
        narrow<int32_t>(mWidth * sizeof(std::uint32_t)), std::to_underlying(Shm::Format::argb8888));

    auto createBuffer2 = mShmPool.create_buffer(
        narrow<int32_t>(mWidth * mHeight * sizeof(std::uint32_t)), narrow<int32_t>(mWidth),
        narrow<int32_t>(mHeight), narrow<int32_t>(mWidth * sizeof(std::uint32_t)),
        std::to_underlying(Shm::Format::argb8888));

    auto queue = co_await use_resource(AsyncQueue<int>::make());

    auto bufferSubscriber1 = [&](IoTask<Buffer> bufferTask) -> IoTask<void> {
      Buffer buffer = co_await std::move(bufferTask);
      Log::d("Created buffer 1");
      mBuffers[0] = buffer;
      co_await queue.push(0);
      co_await when_stop_requested();
      Log::d("Destroying buffer 1");
      buffer.destroy();
    };

    auto bufferSubscriber2 = [&](IoTask<Buffer> bufferTask) -> IoTask<void> {
      Buffer buffer = co_await std::move(bufferTask);
      mBuffers[1] = buffer;
      Log::d("Created buffer 2");
      co_await queue.push(1);
      co_await when_stop_requested();
      Log::d("Destroying buffer 2");
      buffer.destroy();
    };

    mCurrentBufferStopSource.emplace();
    mCurrentBufferScope.emplace();

    mCurrentBufferScope->spawn(std::move(createBuffer1).subscribe(bufferSubscriber1), get_env());
    mCurrentBufferScope->spawn(std::move(createBuffer2).subscribe(bufferSubscriber2), get_env());

    co_await when_all(queue.pop(), queue.pop());
  }

  auto get_current_buffers() -> Observable<std::array<FrameBufferPool::BufferView, 2>> {
    struct BufferObservable {
      FrameBufferPoolContext* mContext;

      static auto do_subscribe(
          FrameBufferPoolContext* context,
          std::function<auto(IoTask<std::array<FrameBufferPool::BufferView, 2>>)->IoTask<void>>
              receiver) -> IoTask<void> {
        co_await use_resource(context->mStrand.lock());
        std::array<FrameBufferPool::BufferView, 2> buffers;
        buffers[0] = FrameBufferPool::BufferView{context->mBuffers[0], context->mPixelViews[0]};
        buffers[1] = FrameBufferPool::BufferView{context->mBuffers[1], context->mPixelViews[1]};
        co_await receiver(coro_just(buffers));
      }

      auto subscribe(
          std::function<auto(IoTask<std::array<FrameBufferPool::BufferView, 2>>)->IoTask<void>>
              receiver) const noexcept -> IoTask<void> {
        return do_subscribe(mContext, std::move(receiver));
      }
    };
    return BufferObservable{this};
  }
};

auto FrameBufferPool::make(Client client) -> Observable<FrameBufferPool> {
  struct FrameBufferPoolObservable {
    Client mClient;

    static auto do_subscribe(Client client,
                             std::function<auto(IoTask<FrameBufferPool>)->IoTask<void>> receiver)
        -> IoTask<void> {
      Strand strand = co_await use_resource(Strand::make());
      FrameBufferPoolContext context(client, strand);
      context.mShm = co_await use_resource(client.bind<Shm>());
      context.mShmPool = co_await use_resource(context.mShm.create_pool(
          context.mShmPoolFd, narrow<int32_t>(context.mShmData.size_bytes())));
      co_await context.resize(Width{640}, Height{480});
      FrameBufferPool pool{context};
      co_await receiver(coro_just(pool));
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

auto FrameBufferPool::get_current_buffers() -> Observable<std::array<BufferView, 2>> {
  return mContext->get_current_buffers();
}

} // namespace cw::protocol