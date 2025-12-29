// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "ImmovableBase.hpp"
#include "concepts.hpp"
#include "queries.hpp"

#include <coroutine>
#include <exception>
#include <utility>
#include <variant>

namespace ms {

template <class Query, class QueryResult, class AwaitingPromise> struct WriteEnvTask;

template <class Query, class QueryResult, class AwaitingPromise> class WriteEnvPromise {
public:
  struct Env;
  struct FinalSuspendAwaiter;

  template <class ChildSender, class ResultType>
  explicit WriteEnvPromise(ChildSender&&, QueryResult&& result,
                           std::coroutine_handle<AwaitingPromise> parentCoro, ResultType*);

  auto get_env() const noexcept -> Env;

  auto get_return_object() noexcept -> WriteEnvTask<Query, QueryResult, AwaitingPromise>;

  auto initial_suspend() noexcept -> std::suspend_always;

  auto final_suspend() noexcept -> FinalSuspendAwaiter;

  void return_void() noexcept;

  void unhandled_exception() noexcept;

  void unhandled_stopped() noexcept;

  template <class Self, class Expression> auto await_transform(this Self& self, Expression&& expr);

private:
  QueryResult mResult;
  std::coroutine_handle<AwaitingPromise> mParentCoro;
};

template <class Query, class QueryResult, class AwaitingPromise>
struct WriteEnvPromise<Query, QueryResult, AwaitingPromise>::Env {
  const WriteEnvPromise* mParent;

  template <class Qry>
    requires(!std::is_same_v<Qry, Query> && callable<Qry, const env_of_t<AwaitingPromise>&>)
  auto query(Qry qry) const noexcept;

  auto query(Query) const noexcept -> QueryResult;
};

template <class Query, class QueryResult, class AwaitingPromise>
struct WriteEnvPromise<Query, QueryResult, AwaitingPromise>::FinalSuspendAwaiter {
  static constexpr auto await_ready() noexcept -> std::false_type;

  auto await_suspend(std::coroutine_handle<WriteEnvPromise> handle) noexcept
      -> std::coroutine_handle<AwaitingPromise>;

  void await_resume() noexcept;
};

template <class Query, class QueryResult, class AwaitingPromise> struct WriteEnvTask {
  using promise_type = WriteEnvPromise<Query, QueryResult, AwaitingPromise>;
  std::coroutine_handle<promise_type> mCoro;
};

template <class Tp> class WriteEnvAwaiter : private ImmovableBase {
public:
  template <class ChildSender, class Query, class QueryResult, class AwaitingPromise>
  explicit WriteEnvAwaiter(ChildSender&& childSender, Query query, QueryResult queryResult,
                           AwaitingPromise& promise);

  ~WriteEnvAwaiter();

  static auto await_ready() noexcept -> std::false_type;

  auto await_suspend(std::coroutine_handle<>) noexcept -> std::coroutine_handle<>;

  auto await_resume() noexcept -> Tp;

private:
  std::variant<std::monostate, std::exception_ptr, Tp> mReturnValue;
  std::coroutine_handle<> mHandle;
};

template <> class WriteEnvAwaiter<void> : private ImmovableBase {
public:
  template <class ChildSender, class Query, class QueryResult, class AwaitingPromise>
  explicit WriteEnvAwaiter(ChildSender&& childSender, Query query, QueryResult mResult,
                           AwaitingPromise& promise);

  ~WriteEnvAwaiter();

  static auto await_ready() noexcept -> std::false_type;

  auto await_suspend(std::coroutine_handle<>) noexcept -> std::coroutine_handle<>;

  auto await_resume() noexcept -> void;

private:
  std::exception_ptr mException;
  std::coroutine_handle<> mHandle;
};

template <class ChildSender, class Query, class QueryResult> class WriteEnvSender {
private:
  template <class AwaitingPromise>
  using env_type = typename WriteEnvPromise<Query, QueryResult, AwaitingPromise>::Env;

  template <class AwaitingPromise>
  using result_type = await_result_t<ChildSender, env_type<AwaitingPromise>>;

public:
  explicit WriteEnvSender(ChildSender&& childSender, QueryResult result);

  template <class Promise>
  auto connect(Promise& promise) noexcept -> WriteEnvAwaiter<result_type<Promise>>;

private:
  ChildSender mChildSender;
  QueryResult mResult;
};

template <class ChildSender, class Query, class QueryResult>
auto write_env(ChildSender childSender, Query, QueryResult result) noexcept
    -> WriteEnvSender<ChildSender, Query, QueryResult>;

/////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation Details                       WriteEnvPromise<Query, Result, AwaitingPromise>

template <class Query, class QueryResult, class AwaitingPromise>
template <class ChildSender, class ResultType>
WriteEnvPromise<Query, QueryResult, AwaitingPromise>::WriteEnvPromise(
    ChildSender&&, QueryResult&& result, std::coroutine_handle<AwaitingPromise> parentCoro,
    ResultType*)
    : mResult(std::move(result)), mParentCoro(parentCoro) {}

template <class Query, class QueryResult, class AwaitingPromise>
auto WriteEnvPromise<Query, QueryResult, AwaitingPromise>::get_env() const noexcept -> Env {
  return Env{this};
}

template <class Query, class QueryResult, class AwaitingPromise>
auto WriteEnvPromise<Query, QueryResult, AwaitingPromise>::initial_suspend() noexcept
    -> std::suspend_always {
  return {};
}

template <class Query, class QueryResult, class AwaitingPromise>
auto WriteEnvPromise<Query, QueryResult, AwaitingPromise>::final_suspend() noexcept
    -> FinalSuspendAwaiter {
  return {};
}

template <class Query, class QueryResult, class AwaitingPromise>
void WriteEnvPromise<Query, QueryResult, AwaitingPromise>::unhandled_stopped() noexcept {
  mParentCoro.promise().unhandled_stopped();
}

template <class Query, class QueryResult, class AwaitingPromise>
template <class Self, class Expression>
auto WriteEnvPromise<Query, QueryResult, AwaitingPromise>::await_transform(this Self& self,
                                                                           Expression&& expr) {
  if constexpr (requires { std::forward<Expression>(expr).connect(self); }) {
    return std::forward<Expression>(expr).connect(self);
  } else {
    return static_cast<Expression&&>(expr);
  }
}

template <class Query, class QueryResult, class AwaitingPromise>
template <class Qry>
  requires(!std::is_same_v<Qry, Query> && callable<Qry, const env_of_t<AwaitingPromise>&>)
auto WriteEnvPromise<Query, QueryResult, AwaitingPromise>::Env::query(Qry qry) const noexcept {
  return qry(::ms::get_env(mParent->mParentCoro.promise()));
}

template <class Query, class QueryResult, class AwaitingPromise>
auto WriteEnvPromise<Query, QueryResult, AwaitingPromise>::Env::query(Query) const noexcept
    -> QueryResult {
  return mParent->mResult;
}

template <class Query, class QueryResult, class AwaitingPromise>
constexpr auto
WriteEnvPromise<Query, QueryResult, AwaitingPromise>::FinalSuspendAwaiter::await_ready() noexcept
    -> std::false_type {
  return {};
}

template <class Query, class QueryResult, class AwaitingPromise>
auto WriteEnvPromise<Query, QueryResult, AwaitingPromise>::FinalSuspendAwaiter::await_suspend(
    std::coroutine_handle<WriteEnvPromise> handle) noexcept
    -> std::coroutine_handle<AwaitingPromise> {
  auto& promise = handle.promise();
  return promise.mParentCoro;
}

template <class Query, class QueryResult, class AwaitingPromise>
void WriteEnvPromise<Query, QueryResult,
                     AwaitingPromise>::FinalSuspendAwaiter::await_resume() noexcept {
  // No-op
}

template <class Query, class QueryResult, class AwaitingPromise>
auto WriteEnvPromise<Query, QueryResult, AwaitingPromise>::get_return_object() noexcept
    -> WriteEnvTask<Query, QueryResult, AwaitingPromise> {
  return WriteEnvTask<Query, QueryResult, AwaitingPromise>{
      std::coroutine_handle<WriteEnvPromise<Query, QueryResult, AwaitingPromise>>::from_promise(
          *this)};
}

template <class Query, class QueryResult, class AwaitingPromise>
void WriteEnvPromise<Query, QueryResult, AwaitingPromise>::return_void() noexcept {
  // No return value for void
}

template <class Query, class QueryResult, class AwaitingPromise>
void WriteEnvPromise<Query, QueryResult, AwaitingPromise>::unhandled_exception() noexcept {
  std::terminate();
}

template <class Tp>
template <class ChildSender, class Query, class QueryResult, class AwaitingPromise>
WriteEnvAwaiter<Tp>::WriteEnvAwaiter(ChildSender&& childSender, Query, QueryResult queryResult,
                                     AwaitingPromise& promise) {
  auto awaitingHandle = std::coroutine_handle<AwaitingPromise>::from_promise(promise);
  auto writeEnvTask = [](ChildSender&& childSender, QueryResult&&,
                         std::coroutine_handle<AwaitingPromise>,
                         std::variant<std::monostate, std::exception_ptr, Tp>* result)
      -> WriteEnvTask<Query, QueryResult, AwaitingPromise> {
    try {
      Tp&& value = co_await std::move(childSender);
      result->template emplace<2>(std::move(value));
    } catch (...) {
      result->template emplace<1>(std::current_exception());
    }
  }(std::move(childSender), std::move(queryResult), awaitingHandle, &mReturnValue);
  mHandle = writeEnvTask.mCoro;
}

template <class Tp> WriteEnvAwaiter<Tp>::~WriteEnvAwaiter() {
  if (mHandle) {
    mHandle.destroy();
  }
}

template <class Tp>
auto WriteEnvAwaiter<Tp>::await_suspend(std::coroutine_handle<>) noexcept
    -> std::coroutine_handle<> {
  return mHandle;
}

template <class Tp> auto WriteEnvAwaiter<Tp>::await_resume() noexcept -> Tp {
  if (mReturnValue.index() == 1) {
    std::rethrow_exception(std::get<1>(mReturnValue));
  } else {
    return std::get<2>(mReturnValue);
  }
}

template <class ChildSender, class Query, class QueryResult, class AwaitingPromise>
WriteEnvAwaiter<void>::WriteEnvAwaiter(ChildSender&& childSender, Query, QueryResult queryResult,
                                       AwaitingPromise& promise) {
  auto awaitingHandle = std::coroutine_handle<AwaitingPromise>::from_promise(promise);
  auto writeEnvTask =
      [](ChildSender&& childSender, QueryResult&&, std::coroutine_handle<AwaitingPromise>,
         std::exception_ptr* onError) -> WriteEnvTask<Query, QueryResult, AwaitingPromise> {
    try {
      co_await std::move(childSender);
    } catch (...) {
      *onError = std::current_exception();
    }
  }(std::move(childSender), std::move(queryResult), awaitingHandle, &mException);
  mHandle = writeEnvTask.mCoro;
  mException = nullptr;
}

template <class ChildSender, class Query, class QueryResult>
WriteEnvSender<ChildSender, Query, QueryResult>::WriteEnvSender(ChildSender&& childSender,
                                                                QueryResult result)
    : mChildSender(std::move(childSender)), mResult(std::move(result)) {}

template <class ChildSender, class Query, class QueryResult>
template <class Promise>
auto WriteEnvSender<ChildSender, Query, QueryResult>::connect(Promise& promise) noexcept
    -> WriteEnvAwaiter<result_type<Promise>> {
  return WriteEnvAwaiter<result_type<Promise>>(std::move(mChildSender), Query{}, std::move(mResult),
                                               promise);
}

template <class ChildSender, class Query, class QueryResult>
auto write_env(ChildSender childSender, Query, QueryResult result) noexcept
    -> WriteEnvSender<ChildSender, Query, QueryResult> {
  return WriteEnvSender<ChildSender, Query, QueryResult>(std::move(childSender), std::move(result));
}

} // namespace ms