// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "ImmovableBase.hpp"
#include "Observable.hpp"

#include <atomic>
#include <coroutine>
#include <stdexcept>

namespace ms {

class AsyncScope;

struct AsyncScopeTask {
  struct promise_type;
};

struct AsyncScopeTask::promise_type {
  AsyncScope& mScope;

  template <class Sender> promise_type(AsyncScope& scope, Sender&&) noexcept : mScope{scope} {}

  auto get_return_object() noexcept -> AsyncScopeTask;

  auto initial_suspend() noexcept -> std::suspend_never;

  auto final_suspend() noexcept -> std::suspend_never;

  void return_void() noexcept;

  void unhandled_exception() noexcept;

  void unhandled_stopped() noexcept;
};

class AsyncScope;

class AsyncScopeHandle {
public:
  explicit AsyncScopeHandle(AsyncScope& scope) noexcept : mScope(&scope) {}

  template <class Sender> void spawn(Sender&& sender);

private:
  AsyncScope* mScope;
};

template <class Sender> struct NestSender;

class AsyncScope : ImmovableBase {
public:
  AsyncScope() = default;

  template <class Sender> void spawn(Sender&& sender) {
    std::ptrdiff_t expected = 1;
    while (!mActiveTasks.compare_exchange_weak(expected, expected + 0b10, std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
      if ((expected & 0b01) == 0) {
        throw std::runtime_error("Cannot spawn new tasks on a stopped AsyncScope");
      }
    }
    [](AsyncScope&, Sender sndr) -> AsyncScopeTask {
      co_return co_await std::forward<Sender>(sndr);
    }(*this, std::forward<Sender>(sender));
  }

  template <class Sender> auto nest(Sender&& sender) -> NestSender<Sender>;

  struct CloseAwaitable;

  auto close() noexcept -> CloseAwaitable;

private:
  friend struct AsyncScopeTask::promise_type;
  std::atomic<std::ptrdiff_t> mActiveTasks{1};
  std::coroutine_handle<> mWaitingHandle;
};

struct AsyncScope::CloseAwaitable {
  AsyncScope& mScope;

  static constexpr auto await_ready() noexcept -> std::false_type { return {}; }

  auto await_suspend(std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>;

  void await_resume() noexcept;
};

auto create_scope() -> Observable<AsyncScopeHandle>;

template <class Sender> void AsyncScopeHandle::spawn(Sender&& sender) {
  mScope->spawn(std::forward<Sender>(sender));
}

} // namespace ms