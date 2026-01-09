// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "write_env.hpp"

#include <exception>

namespace cw {

WriteEnvAwaiter<void>::~WriteEnvAwaiter() {
  if (mHandle) {
    mHandle.destroy();
  }
}

auto WriteEnvAwaiter<void>::await_ready() noexcept -> std::false_type { return {}; }

auto WriteEnvAwaiter<void>::await_suspend(std::coroutine_handle<>) noexcept
    -> std::coroutine_handle<> {
  return mHandle;
}

auto WriteEnvAwaiter<void>::await_resume() -> void {
  if (mException) {
    std::rethrow_exception(mException);
  }
}

} // namespace cw