// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "JinjaTemplateEngine.hpp"

namespace ms {

JinjaContext::JinjaContext(const std::string& string)
: mStorage(string)
{
}

JinjaContext::JinjaContext(const std::map<std::string, JinjaContext>& object)
: mStorage(object)
{
}

JinjaContext::JinjaContext(const std::vector<JinjaContext>& array)
: mStorage(array)
{
}

auto JinjaContext::isString() const noexcept -> bool
{
    return mStorage.index() == 0;
}

auto JinjaContext::isObject() const noexcept -> bool
{
    return mStorage.index() == 1;
}

auto JinjaContext::isArray() const noexcept -> bool
{
    return mStorage.index() == 2;
}

auto JinjaContext::asString() const -> const std::string&
{
    return std::get<0>(mStorage);
}

auto JinjaContext::asObject() const -> const std::map<std::string, JinjaContext>&
{
    return std::get<1>(mStorage);
}

auto JinjaContext::asArray() const -> const std::vector<JinjaContext>&
{
    return std::get<2>(mStorage);
}

} // namespace ms