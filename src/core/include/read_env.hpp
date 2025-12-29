// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <coroutine>
#include <optional>
#include <utility>

namespace ms {

template <class ResultType> class ReadEnvAwaiter {
private:
  ResultType mResult;

public:
  explicit ReadEnvAwaiter(ResultType result) : mResult(std::move(result)) {}

  static auto await_ready() noexcept -> std::true_type { return {}; }

  void await_suspend(std::coroutine_handle<>) noexcept {}

  auto await_resume() noexcept -> ResultType { return mResult; }
};

template <class Query> class ReadEnvSender {
public:
  explicit ReadEnvSender(Query query) : mQuery(std::move(query)) {}

  template <class Promise> auto connect(Promise& promise) noexcept {
    return ReadEnvAwaiter{mQuery(::ms::get_env(promise))};
  }

private:
  Query mQuery;
};

template <class Query> auto read_env(Query query) noexcept -> ReadEnvSender<Query> {
  return ReadEnvSender<Query>(std::move(query));
}

} // namespace ms