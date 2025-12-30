// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <ostream>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ms {

class JinjaContext {
public:
  explicit JinjaContext(const std::string& string);
  explicit JinjaContext(const std::map<std::string, JinjaContext>& object);
  explicit JinjaContext(const std::vector<JinjaContext>& array);

  auto isString() const noexcept -> bool;
  auto isObject() const noexcept -> bool;
  auto isArray() const noexcept -> bool;

  auto asString() const -> const std::string&;
  auto asObject() const -> const std::map<std::string, JinjaContext>&;
  auto asArray() const -> const std::vector<JinjaContext>&;

  friend bool operator==(const JinjaContext&, const JinjaContext&) = default;

private:
  std::variant<std::string, std::map<std::string, JinjaContext>, std::vector<JinjaContext>> mStorage;
};

void generate_from_template(const JinjaContext& context, std::string_view templateContent,
                            std::ostream& out);

} // namespace ms