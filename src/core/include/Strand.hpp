// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "Observable.hpp"

namespace ms {

struct StrandContext;

class Strand {
public:
  static auto make() -> Observable<Strand>;

  auto lock() -> Observable<void>;

private:
  explicit Strand(StrandContext& context) noexcept : mContext(&context) {}
  StrandContext* mContext;
};

} // namespace ms