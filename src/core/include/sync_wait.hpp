// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "IoContext.hpp"
#include "IoTask.hpp"
#include "concepts.hpp"

#include <exception>
#include <optional>
#include <variant>

namespace ms {

struct SyncWaitEnv {
  IoScheduler scheduler;

  auto query(get_scheduler_t) const noexcept -> IoScheduler { return scheduler; }
};

template <class ValueType> struct SyncWaitState {
  IoContext* context;
  std::mutex mutex;
  std::variant<std::monostate, std::exception_ptr, ValueType> result;

  explicit SyncWaitState(IoContext* ctx) : context(ctx) {}

  auto get_env() const noexcept -> SyncWaitEnv { return SyncWaitEnv{context->get_scheduler()}; }

  auto get_result() -> std::optional<ValueType> {
    std::lock_guard lock(mutex);
    if (result.index() == 1) {
      std::rethrow_exception(std::get<1>(result));
    } else if (result.index() == 2) {
      return std::get<2>(result);
    } else {
      return std::nullopt;
    }
  }
};

template <> struct SyncWaitState<void> {
  IoContext* context;
  std::mutex mutex;
  std::variant<std::monostate, std::exception_ptr, std::monostate> result;

  explicit SyncWaitState(IoContext* ctx) : context(ctx) {}

  auto get_env() const noexcept -> SyncWaitEnv { return SyncWaitEnv{context->get_scheduler()}; }

  auto get_result() -> bool {
    std::lock_guard lock(mutex);
    if (result.index() == 1) {
      std::rethrow_exception(std::get<1>(result));
    }
    return result.index() == 2;
  }
};

template <class ValueType> struct SyncWaitTask {
  struct promise_type {
    explicit promise_type(SyncWaitState<ValueType>* state) : mState(state) {}

    auto get_return_object() noexcept -> SyncWaitTask {
      return SyncWaitTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    auto initial_suspend() noexcept -> std::suspend_never { return {}; }

    auto final_suspend() noexcept -> std::suspend_always { return {}; }

    void return_value(ValueType value) noexcept {
      std::lock_guard lock(mState->mutex);
      mState->result.template emplace<2>(std::move(value));
      mState->context->request_stop();
    }

    void unhandled_exception() noexcept {
      std::lock_guard lock(mState->mutex);
      mState->result.template emplace<1>(std::current_exception());
      mState->context->request_stop();
    }

    void unhandled_stopped() noexcept {
      // Treat stopped as no result
      mState->context->request_stop();
    }

    auto get_env() const noexcept -> SyncWaitEnv { return mState->get_env(); }

    SyncWaitState<ValueType>* mState;
  };

  std::coroutine_handle<promise_type> mHandle;
};

template <> struct SyncWaitTask<void> {
  struct promise_type {
    explicit promise_type(SyncWaitState<void>* state) : mState(state) {}

    auto get_return_object() noexcept -> SyncWaitTask {
      return SyncWaitTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    auto initial_suspend() noexcept -> std::suspend_never { return {}; }

    auto final_suspend() noexcept -> std::suspend_always { return {}; }

    void return_void() noexcept {
      std::lock_guard lock(mState->mutex);
      mState->result.template emplace<2>();
      mState->context->request_stop();
    }

    void unhandled_exception() noexcept {
      std::lock_guard lock(mState->mutex);
      mState->result.template emplace<1>(std::current_exception());
      mState->context->request_stop();
    }

    void unhandled_stopped() noexcept {
      // Treat stopped as no result
      mState->context->request_stop();
    }

    auto get_env() const noexcept -> SyncWaitEnv { return mState->get_env(); }

    SyncWaitState<void>* mState;
  };
  std::coroutine_handle<promise_type> mHandle;
};

template <class Awaitable> auto sync_wait(Awaitable&& awaitable) {
  using ValueType = ms::await_result_t<Awaitable, SyncWaitEnv>;
  IoContext ioContext;
  SyncWaitState<ValueType> state{&ioContext};
  auto task = [&](SyncWaitState<ValueType>*) -> SyncWaitTask<ValueType> {
    co_return co_await static_cast<Awaitable&&>(awaitable);
  }(&state);
  ioContext.run();
  task.mHandle.destroy();
  return state.get_result();
}

} // namespace ms