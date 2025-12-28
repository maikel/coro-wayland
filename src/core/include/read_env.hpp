// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <queries.hpp>

#include <coroutine>
#include <optional>

namespace ms {

template <class Query, class Promise>
class ReadEnvAwaiter {
private:
  using ResultType = decltype(std::declval<Query>()(std::declval<env_of_t<Promise>>()));

  [[no_unique_address]] Query mQuery;
  std::optional<ResultType> mResult;

public:
    explicit ReadEnvAwaiter(Query query) : mQuery(std::move(query)) {}
    
    static auto await_ready() noexcept -> std::false_type {
        return {};
    }
    
    auto await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto env = ms::get_env(handle.promise());
        mResult.emplace(mQuery(env));
        return handle;
    }
    
    auto await_resume() noexcept -> ResultType {
        return mResult.value();
    }
};

template <class Query>
class ReadEnvSender {
public:
    explicit ReadEnvSender(Query query) : mQuery(std::move(query)) {}
    
    template <class Promise>
    auto connect(Promise&) noexcept {
        return ReadEnvAwaiter<Query, Promise>(mQuery);
    }
private:
    Query mQuery;
};

template <class Query>
auto read_env(Query query) noexcept -> ReadEnvSender<Query> {
    return ReadEnvSender<Query>(std::move(query));
}

} // namespace ms