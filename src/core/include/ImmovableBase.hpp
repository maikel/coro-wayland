// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

namespace cw {

struct ImmovableBase {
  ImmovableBase() = default;
  ~ImmovableBase() = default;

  ImmovableBase(const ImmovableBase&) = delete;
  ImmovableBase& operator=(const ImmovableBase&) = delete;
  ImmovableBase(ImmovableBase&&) = delete;
  ImmovableBase& operator=(ImmovableBase&&) = delete;
};

} // namespace cw