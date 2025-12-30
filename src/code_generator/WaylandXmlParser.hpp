// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include "JinjaTemplateEngine.hpp"

#include <string_view>

namespace ms {

auto parse_wayland_xml(std::string_view xmlContent) -> JinjaContext;

} // namespace ms