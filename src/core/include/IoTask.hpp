// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "IoContext.hpp"
#include "Task.hpp"

namespace cw {

class IoTaskEnv;

/// Virtual function table for type-erased task contexts.
/// Enables runtime polymorphism for parent promise operations.
struct IoTaskContextVtable {
  auto (*get_continuation)(void*) noexcept -> std::coroutine_handle<>;
  auto (*get_stop_token)(const void*) noexcept -> std::stop_token;
  auto (*get_scheduler)(const void*) noexcept -> IoScheduler;
  void (*set_stopped)(void*) noexcept;
};

/// Compile-time vtable instance for specific promise type.
/// Static functions cast void* back to concrete promise type.
template <class AwaitingPromise>
inline constexpr IoTaskContextVtable IoTaskContextVtableFor = {
    /*get_continuation*/ +[](void* pointer) noexcept -> std::coroutine_handle<> {
      auto* promise = static_cast<AwaitingPromise*>(pointer);
      return std::coroutine_handle<AwaitingPromise>::from_promise(*promise);
    },
    /*get_stop_token*/
    +[](const void* pointer) noexcept -> std::stop_token {
      auto* promise = static_cast<const AwaitingPromise*>(pointer);
      return ::cw::get_stop_token(::cw::get_env(*promise));
    },
    /*get_scheduler*/
    +[](const void* pointer) noexcept -> IoScheduler {
      auto* promise = static_cast<const AwaitingPromise*>(pointer);
      return ::cw::get_scheduler(::cw::get_env(*promise));
    },
    /*set_stopped*/
    +[](void* pointer) noexcept {
      auto* promise = static_cast<AwaitingPromise*>(pointer);
      if constexpr (requires { promise->unhandled_stopped(); }) {
        promise->unhandled_stopped();
      } else {
        try {
          throw std::system_error(std::make_error_code(std::errc::operation_canceled));
        } catch (...) {
          promise->unhandled_exception();
        }
      }
    }};

class IoTaskContinuation {
public:
  IoTaskContinuation() = default;

  IoTaskContinuation(const IoTaskContextVtable* vtable, void* promise) noexcept
      : mVtable(vtable), mPromise(promise) {}

  auto get_handle() const noexcept -> std::coroutine_handle<> {
    return mVtable->get_continuation(mPromise);
  }

  void set_stopped() noexcept { mVtable->set_stopped(mPromise); }

  auto get_env() const noexcept {
    class AnyEnv {
    private:
      const IoTaskContinuation* mContext;

    public:
      explicit AnyEnv(const IoTaskContinuation* context) noexcept : mContext(context) {}

      auto query(get_stop_token_t) const noexcept -> std::stop_token {
        return mContext->mVtable->get_stop_token(mContext->mPromise);
      }
      auto query(get_scheduler_t) const noexcept -> IoScheduler {
        return mContext->mVtable->get_scheduler(mContext->mPromise);
      }
    };
    return AnyEnv{this};
  }

private:
  const IoTaskContextVtable* mVtable{};
  void* mPromise{};
};

/// Type-erased task context using vtable dispatch.
/// Stores void* to parent promise with vtable for dynamic operations.
class IoTaskContextBase {
public:
  template <class AwaitingPromise>
  explicit IoTaskContextBase(std::coroutine_handle<AwaitingPromise> awaitingHandle) noexcept;

  template <class AwaitingPromise>
  auto reset_continuation(std::coroutine_handle<AwaitingPromise> awaitingHandle) noexcept
      -> IoTaskContinuation;

  auto get_continuation() const noexcept -> std::coroutine_handle<>;

  auto get_stop_token() const noexcept -> std::stop_token;

  auto get_scheduler() const noexcept -> IoScheduler;

  void set_stopped() noexcept;

private:
  const IoTaskContextVtable* mVtable{};
  void* mPromise{};
};

/// Environment for task execution providing stop token queries.
/// Encapsulates cancellation state propagated from parent coroutines.
class IoTaskEnv {
private:
  const IoTaskContextBase* mContext{};

public:
  explicit IoTaskEnv(const IoTaskContextBase* context) noexcept;

  auto query(get_stop_token_t) const noexcept -> std::stop_token;
  auto query(get_scheduler_t) const noexcept -> IoScheduler;
};

/// Task context with statically-known parent promise type.
/// Directly stores parent handle, avoiding indirection.
class IoTaskContext : public IoTaskContextBase {
public:
  template <class AwaitingPromise>
  explicit IoTaskContext(std::coroutine_handle<AwaitingPromise> awaitingHandle) noexcept;

  auto get_env() const noexcept -> IoTaskEnv;
};

/// Default traits for Task, defining context and environment types.
struct IoTaskTraits {
  using context_type = IoTaskContext;
  using env_type = IoTaskEnv;
};

template <class Tp> using IoTask = BasicTask<Tp, IoTaskTraits>;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                                  IoTaskContextBase

template <class AwaitingPromise>
IoTaskContextBase::IoTaskContextBase(
    std::coroutine_handle<AwaitingPromise> awaitingHandle) noexcept {
  mVtable = &IoTaskContextVtableFor<AwaitingPromise>;
  mPromise = static_cast<void*>(&awaitingHandle.promise());
}

template <class AwaitingPromise>
auto IoTaskContextBase::reset_continuation(
    std::coroutine_handle<AwaitingPromise> awaitingHandle) noexcept -> IoTaskContinuation {
  IoTaskContinuation old{mVtable, mPromise};
  mVtable = &IoTaskContextVtableFor<AwaitingPromise>;
  mPromise = static_cast<void*>(&awaitingHandle.promise());
  return old;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                                    IoTaskContext

template <class AwaitingPromise>
IoTaskContext::IoTaskContext(std::coroutine_handle<AwaitingPromise> awaitingHandle) noexcept
    : IoTaskContextBase(awaitingHandle) {}

} // namespace cw