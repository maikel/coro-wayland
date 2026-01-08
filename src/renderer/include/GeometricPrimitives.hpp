// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

namespace ms {

struct Point {
  int x;
  int y;
};

struct Line {
  Point start;
  Point end;
};

} // namespace ms