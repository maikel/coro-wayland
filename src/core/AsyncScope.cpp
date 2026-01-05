// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "AsyncScope.hpp"

namespace ms {

auto AsyncScopeTask::promise_type::get_return_object() noexcept -> AsyncScopeTask {
  return AsyncScopeTask{};
}

auto AsyncScopeTask::promise_type::initial_suspend() noexcept -> std::suspend_never { return {}; }

auto AsyncScopeTask::promise_type::final_suspend() noexcept -> std::suspend_never { return {}; }

void AsyncScopeTask::promise_type::return_void() noexcept {
  if (mScope.mActiveTasks.fetch_sub(0b10, std::memory_order_acq_rel) == 0b10) {
    mScope.mWaitingHandle.resume();
  }
}

void AsyncScopeTask::promise_type::unhandled_exception() noexcept { return_void(); }

void AsyncScopeTask::promise_type::unhandled_stopped() noexcept {
  return_void();
  std::coroutine_handle<promise_type>::from_promise(*this).destroy();
}

auto AsyncScope::CloseAwaitable::await_suspend(std::coroutine_handle<> handle) noexcept
    -> std::coroutine_handle<> {
  mScope.mWaitingHandle = handle;
  if (mScope.mActiveTasks.fetch_sub(1, std::memory_order_acquire) == 1) {
    return handle;
  }
  return std::noop_coroutine();
}

void AsyncScope::CloseAwaitable::await_resume() noexcept {}

auto AsyncScope::close() noexcept -> CloseAwaitable { return CloseAwaitable{*this}; }

} // namespace ms