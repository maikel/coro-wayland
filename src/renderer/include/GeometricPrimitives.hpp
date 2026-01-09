// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

namespace cw {

struct Point {
  int x;
  int y;
};

struct Line {
  Point start;
  Point end;
};

} // namespace cw