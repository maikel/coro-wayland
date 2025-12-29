// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "IoTask.hpp"

namespace ms {

////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                                  IoTaskContextBase

auto IoTaskContextBase::get_continuation() const noexcept -> std::coroutine_handle<> {
  return mVtable->get_continuation(mPromise);
}

auto IoTaskContextBase::get_stop_token() const noexcept -> std::stop_token {
  return mVtable->get_stop_token(mPromise);
}

auto IoTaskContextBase::get_scheduler() const noexcept -> IoScheduler {
  return mVtable->get_scheduler(mPromise);
}

void IoTaskContextBase::set_stopped() noexcept { mVtable->set_stopped(mPromise); }

///////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                                                    IoTaskEnv

IoTaskEnv::IoTaskEnv(const IoTaskContextBase* context) noexcept : mContext(context) {}

auto IoTaskEnv::query(get_stop_token_t) const noexcept -> std::stop_token {
  return mContext->get_stop_token();
}

auto IoTaskEnv::query(get_scheduler_t) const noexcept -> IoScheduler {
  return mContext->get_scheduler();
}

auto IoTaskContext::get_env() const noexcept -> IoTaskEnv { return IoTaskEnv{this}; }

} // namespace ms