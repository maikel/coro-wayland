// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "Task.hpp"

namespace ms {

void TaskResult<void>::set_error(std::exception_ptr error) noexcept {
  mResult.template emplace<1>(error);
}

void TaskResult<void>::set_value() noexcept { mResult.template emplace<2>(std::monostate{}); }

auto TaskResult<void>::get_result() -> void {
  if (mResult.index() == 1) {
    std::rethrow_exception(std::get<1>(mResult));
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details TaskContext<AwaitingPromise>

auto TaskContext::get_env() const noexcept -> TaskEnv { return mVtable->get_env(mPromise); }

void TaskContext::set_stopped() noexcept { mVtable->set_stopped(mPromise); }

auto TaskContext::get_continuation() const noexcept -> std::coroutine_handle<> {
  return mVtable->get_continuation(mPromise);
}

TaskEnv::TaskEnv(std::stop_token stopToken) noexcept : mStopToken(stopToken) {}

auto TaskEnv::query(get_stop_token_t) const noexcept -> std::stop_token { return mStopToken; }

} // namespace ms