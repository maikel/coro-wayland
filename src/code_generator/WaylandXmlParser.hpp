// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cw {

class XmlNode;

struct XmlTag {
  std::string name;
  std::vector<std::pair<std::string, std::string>> attributes;
  std::vector<XmlNode> children;
};

class XmlNode {
public:
  explicit XmlNode(const std::string& text);
  explicit XmlNode(const XmlTag& tag);

  auto isText() const noexcept -> bool;
  auto isTag() const noexcept -> bool;

  auto asText() const -> const std::string&;
  auto asTag() const -> const XmlTag&;

private:
  std::variant<std::string, XmlTag> mStorage;
};

auto parse_wayland_xml(std::string_view xmlContent) -> XmlTag;

} // namespace cw