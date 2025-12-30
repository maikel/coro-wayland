// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "JinjaTemplateEngine.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <span>

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

void TemplateDocument::render(const JinjaContext& context, std::ostream& out) const {
  if (mRenderFunc) {
    mRenderFunc(mDocument, context, out);
  }
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
    tokens.push_back(Token{Token::Type::EndOfFile, ""});
    return tokens;
  }
};

auto tokenize(std::string_view templateContent) -> std::vector<Token> {
  Lexer lexer{templateContent, 0};
  return lexer.tokenize();
}

struct TextNode {
  std::string_view content;

  void render(const JinjaContext& /* context */, std::ostream& out) const { out << content; }
};

struct EmptyDocument {
  void render(const JinjaContext& /* context */, std::ostream& /* out */) const {
    // Do nothing
  }
};

struct SubstitutionNode {
    std::string_view variableName;

    void render(const JinjaContext& context, std::ostream& out) const {
        if (context.isObject()) {
            const auto& obj = context.asObject();
            auto it = obj.find(std::string(variableName));
            if (it == obj.end()) {
                throw std::runtime_error("Variable '" + std::string(variableName) + "' not found in context");
            }
            if (!it->second.isString()) {
                throw std::runtime_error("Variable '" + std::string(variableName) + "' is not a string");
            }
            out << it->second.asString();
        }
    }
};

struct MultipleNodes {
  std::vector<TemplateDocument> documents;

  void render(const JinjaContext& context, std::ostream& out) const {
    for (const auto& doc : documents) {
      doc.render(context, out);
    }
  }
};

struct ParserResult {
  TemplateDocument document;
  std::span<const Token> remainingTokens;
};

auto parse_block(std::span<const Token> tokens) -> ParserResult {
  // Parsing implementation would go here
  return ParserResult{TemplateDocument{EmptyDocument{}}, tokens};
}

auto parse_substitution(std::span<const Token> tokens) -> ParserResult {
  assert(tokens[0].type == Token::Type::VariableStart);
  if (tokens.size() < 3) {
    throw std::runtime_error("Unexpected end of tokens in substitution");
  }
  if (tokens[1].type != Token::Type::Identifier) {
    throw std::runtime_error("Expected identifier in substitution");
  }
  if (tokens[2].type != Token::Type::VariableEnd) {
    throw std::runtime_error("Expected variable end token in substitution");
  }
  return ParserResult{TemplateDocument{SubstitutionNode{tokens[1].value}}, tokens.subspan(3)};
}

auto parse_next_document(std::span<const Token> tokens) -> ParserResult {
  if (tokens.empty()) {
    return ParserResult{TemplateDocument{EmptyDocument{}}, tokens};
  }
  switch (tokens[0].type) {
  case Token::Type::Text:
    return ParserResult{TemplateDocument{TextNode{tokens[0].value}}, tokens.subspan(1)};
  case Token::Type::VariableStart:
    return parse_substitution(tokens);
  case Token::Type::BlockStart:
    return parse_block(tokens);
  case Token::Type::EndOfFile:
    return ParserResult{TemplateDocument{EmptyDocument{}}, tokens};
  case Token::Type::VariableEnd:
    [[fallthrough]];
  case Token::Type::BlockEnd:
    [[fallthrough]];
  case Token::Type::If:
    [[fallthrough]];
  case Token::Type::EndIf:
    [[fallthrough]];
  case Token::Type::For:
    [[fallthrough]];
  case Token::Type::EndFor:
    [[fallthrough]];
  case Token::Type::In:
    [[fallthrough]];
  case Token::Type::Identifier:
    [[fallthrough]];
  case Token::Type::StringLiteral:
    throw std::runtime_error("Unexpected token type in template");
  }

  throw std::runtime_error("Unsupported token type in template");
}

} // namespace

auto make_document(std::string_view templateContent) -> TemplateDocument {
  [[maybe_unused]] auto tokens = tokenize(templateContent);
  std::vector<TemplateDocument> documents;
  ParserResult result = parse_next_document(tokens);
  while (!result.remainingTokens.empty()) {
    documents.push_back(std::move(result.document));
    result = parse_next_document(result.remainingTokens);
  }
  return TemplateDocument{MultipleNodes{documents}};
}

} // namespace ms