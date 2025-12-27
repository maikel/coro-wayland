// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

namespace ms {

template <class Fn, class... Args>
concept callable = requires(Fn&& fn, Args&&... args) { fn(static_cast<Args &&>(args)...); };

} // namespace ms