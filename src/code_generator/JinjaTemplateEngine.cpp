// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "JinjaTemplateEngine.hpp"

namespace ms {

JinjaContext::JinjaContext(const std::string& string) : mStorage(string) {}

JinjaContext::JinjaContext(const std::map<std::string, JinjaContext>& object) : mStorage(object) {}

JinjaContext::JinjaContext(const std::vector<JinjaContext>& array) : mStorage(array) {}

auto JinjaContext::isString() const noexcept -> bool { return mStorage.index() == 0; }

auto JinjaContext::isObject() const noexcept -> bool { return mStorage.index() == 1; }

auto JinjaContext::isArray() const noexcept -> bool { return mStorage.index() == 2; }

auto JinjaContext::asString() const -> const std::string& { return std::get<0>(mStorage); }

auto JinjaContext::asObject() const -> const std::map<std::string, JinjaContext>& {
  return std::get<1>(mStorage);
}

auto JinjaContext::asArray() const -> const std::vector<JinjaContext>& {
  return std::get<2>(mStorage);
}

namespace {
struct Token {
  enum class Type {
    Text,          // Plain text
    VariableStart, // {{
    VariableEnd,   // }}
    BlockStart,    // {%
    BlockEnd,      // %}
    If,            // if
    EndIf,         // endif
    For,           // for
    EndFor,        // endfor
    In,            // in
    Identifier,
    StringLiteral,
    EndOfFile
  };
  Type type;
  std::string_view value;
};

struct Lexer {
  std::string_view mInput;
  std::size_t mPosition = 0;

  auto tokenize() -> std::vector<Token> {
    std::vector<Token> tokens;
    // Lexer implementation would go here
    return tokens;
  }
};

auto tokenize(std::string_view templateContent) -> std::vector<Token> { 
    Lexer lexer{templateContent, 0};
    return lexer.tokenize();
}

struct TextDocument {
  std::string_view content;

  void render(const JinjaContext& /* context */, std::ostream& out) const {
    out << content;
  }
};

} // namespace

auto make_document(std::string_view templateContent) -> TemplateDocument
{
    [[maybe_unused]] auto tokens = tokenize(templateContent);
    TextDocument doc{templateContent};
    return TemplateDocument{doc};
}

} // namespace ms