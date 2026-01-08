// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <coroutine>
#include <stop_token>

namespace ms {

struct EmptyEnv {};

struct get_env_t {
  template <class Promise> auto operator()(const Promise& promise) const noexcept {
    if constexpr (requires { promise.get_env(); }) {
      return promise.get_env();
    } else {
      return EmptyEnv{};
    }
  }
};
inline constexpr get_env_t get_env{};

template <class EnvProvider> using env_of_t = decltype(get_env(std::declval<EnvProvider>()));

struct get_stop_token_t {
  template <class Env> auto operator()(const Env& env) const noexcept -> std::stop_token {
    if constexpr (requires { env.query(*this); }) {
      return env.query(*this);
    } else {
      return std::stop_token{};
    }
  }
};
inline constexpr get_stop_token_t get_stop_token{};

struct get_scheduler_t {
  template <class Env>
  auto operator()(const Env& env) const noexcept
    requires requires { env.query(*this); }
  {
    return env.query(*this);
  }
};
inline constexpr get_scheduler_t get_scheduler{};

template <class AwaiterPromise> struct GetThisHandle {
  static auto await_ready() noexcept -> std::true_type { return {}; }

  auto await_suspend(std::coroutine_handle<AwaiterPromise>) noexcept -> void {}

  auto await_resume() noexcept -> std::coroutine_handle<AwaiterPromise> { return mHandle; }

  std::coroutine_handle<AwaiterPromise> mHandle;
};

struct GetThisHandleSender {
  template <class AwaiterPromise>
  auto connect(AwaiterPromise& promise) const noexcept -> GetThisHandle<AwaiterPromise> {
    return GetThisHandle<AwaiterPromise>{
        std::coroutine_handle<AwaiterPromise>::from_promise(promise)};
  }
};

inline auto get_this_handle() noexcept -> GetThisHandleSender { return GetThisHandleSender{}; }

} // namespace ms