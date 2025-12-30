// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "JinjaTemplateEngine.hpp"

#include <algorithm>
#include <cassert>
#include <span>
#include <stdexcept>

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
    Else,          // else
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

auto make_document(std::span<const Token> tokens) -> TemplateDocument;

struct Lexer {
  std::string_view mInput;
  std::size_t mPosition = 0;

  void skip_whitespace() {
    while (mPosition < mInput.size() && std::isspace(mInput[mPosition])) {
      ++mPosition;
    }
  }

  static constexpr std::string_view ifLiteral = "if";
  static constexpr std::string_view elseLiteral = "else";
  static constexpr std::string_view endifLiteral = "endif";
  static constexpr std::string_view forLiteral = "for";
  static constexpr std::string_view endforLiteral = "endfor";
  static constexpr std::string_view inLiteral = "in";

  auto tokenize() -> std::vector<Token> {
    std::vector<Token> tokens;
    while (mPosition < mInput.size()) {
      if (mInput.substr(mPosition).starts_with("{{")) {
        tokens.push_back(Token{Token::Type::VariableStart, "{{"});
        mPosition += 2;
        skip_whitespace();
        std::string_view remaining = mInput.substr(mPosition);
        std::size_t nameEnd = remaining.find_first_of(" \t\r}");
        if (nameEnd == std::string_view::npos) {
          throw std::runtime_error("Unterminated variable substitution");
        }
        std::string_view name = remaining.substr(0, nameEnd);
        tokens.push_back(Token{Token::Type::Identifier, name});
        mPosition += nameEnd;
        skip_whitespace();
        if (mInput.substr(mPosition).starts_with("}}")) {
          tokens.push_back(Token{Token::Type::VariableEnd, "}}"});
          mPosition += 2;
        } else {
          throw std::runtime_error("Expected '}}' at the end of variable substitution");
        }
      } else if (mInput.substr(mPosition).starts_with("{%")) {
        tokens.push_back(Token{Token::Type::BlockStart, "{%"});
        mPosition += 2;
        skip_whitespace();
        std::string_view remaining = mInput.substr(mPosition);
        while (!remaining.empty() && !remaining.starts_with("%}")) {
          if (remaining.starts_with(ifLiteral)) {
            tokens.push_back(Token{Token::Type::If, ifLiteral});
            mPosition += ifLiteral.size();
          } else if (remaining.starts_with(elseLiteral)) {
            tokens.push_back(Token{Token::Type::Else, elseLiteral});
            mPosition += elseLiteral.size();
          } else if (remaining.starts_with(endifLiteral)) {
            tokens.push_back(Token{Token::Type::EndIf, endifLiteral});
            mPosition += endifLiteral.size();
          } else if (remaining.starts_with(forLiteral)) {
            tokens.push_back(Token{Token::Type::For, forLiteral});
            mPosition += forLiteral.size();
          } else if (remaining.starts_with(endforLiteral)) {
            tokens.push_back(Token{Token::Type::EndFor, endforLiteral});
            mPosition += endforLiteral.size();
          } else if (remaining.starts_with(inLiteral)) {
            tokens.push_back(Token{Token::Type::In, inLiteral});
            mPosition += inLiteral.size();
          } else {
            std::size_t nameEnd = remaining.find_first_of(" \t\r%}");
            if (nameEnd == std::string_view::npos) {
              throw std::runtime_error("Unterminated block");
            }
            std::string_view name = remaining.substr(0, nameEnd);
            tokens.push_back(Token{Token::Type::Identifier, name});
            mPosition += nameEnd;
          }
          skip_whitespace();
          remaining = mInput.substr(mPosition);
        }
        if (mInput.substr(mPosition).starts_with("%}")) {
          tokens.push_back(Token{Token::Type::BlockEnd, "%}"});
          mPosition += 2;
        } else {
          throw std::runtime_error("Expected '%}' at the end of block");
        }
      } else {
        std::size_t substitutionStart = mInput.find("{{", mPosition);
        std::size_t blockStart = mInput.find("{%", mPosition);
        std::size_t textEnd = std::min(substitutionStart, blockStart);
        if (textEnd == std::string_view::npos) {
          textEnd = mInput.size();
        }
        tokens.push_back(Token{Token::Type::Text, mInput.substr(mPosition, textEnd - mPosition)});
        mPosition = textEnd;
      }
    }
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
  std::string_view identifierPath;

  static auto get_next_identifier(std::string_view identifier) -> std::string_view {
    auto nextPos = identifier.find_first_of(".[");
    if (nextPos == std::string_view::npos) {
      return identifier;
    }
    return identifier.substr(0, nextPos);
  }

  static auto get_destination_context(const JinjaContext& context, std::string_view identifier)
      -> const JinjaContext& {
    std::string_view prevVar = identifier;
    const JinjaContext* currentContext = &context;
    while (!prevVar.empty()) {
      const JinjaObject* currentObject = &currentContext->asObject();
      std::string nextVar(get_next_identifier(prevVar));
      auto it = currentObject->find(nextVar);
      if (it == currentObject->end()) {
        throw std::runtime_error("Variable '" + nextVar + "' not found in context");
      }
      const JinjaContext& nextContext = it->second;
      if (nextVar.size() == prevVar.size()) {
        return nextContext;
      }
      prevVar = prevVar.substr(nextVar.size());
      if (nextContext.isString()) {
        throw std::runtime_error("Cannot access sub-property of a string variable");
      } else if (nextContext.isObject()) {
        if (!prevVar.starts_with(".")) {
          throw std::runtime_error("Expected '.' after object variable");
        }
        prevVar = prevVar.substr(1);
        currentContext = &nextContext;
      } else if (nextContext.isArray()) {
        prevVar = prevVar.substr(nextVar.size());
        const JinjaArray* currentArray = &nextContext.asArray();
        while (true) {
          if (!prevVar.starts_with("[")) {
            throw std::runtime_error("Expected '[' after array variable");
          }
          std::size_t closingBracket = prevVar.find("]");
          if (closingBracket == std::string_view::npos) {
            throw std::runtime_error("Expected closing ']' for array index");
          }
          std::string_view indexStr = prevVar.substr(1, closingBracket - 1);
          std::size_t index = std::stoul(std::string(indexStr));
          if (index >= currentArray->size()) {
            throw std::runtime_error("Array index out of bounds");
          }
          const JinjaContext& arrayItem = (*currentArray)[index];
          if (arrayItem.isObject()) {
            currentContext = &arrayItem;
            prevVar = prevVar.substr(closingBracket + 1);
            break;
          } else if (arrayItem.isString()) {
            if (closingBracket + 1 != prevVar.size()) {
              throw std::runtime_error("Cannot access sub-property of a string variable");
            }
            return arrayItem;
          } else {
            assert(arrayItem.isArray());
            currentArray = &arrayItem.asArray();
            prevVar = prevVar.substr(closingBracket + 1);
          }
        }
      } else {
        throw std::runtime_error("Unsupported variable type");
      }
    }
    return *currentContext;
  }

  void render(const JinjaContext& context, std::ostream& out) const {
    const JinjaContext& destContext = get_destination_context(context, identifierPath);
    if (!destContext.isString()) {
      throw std::runtime_error("Substitution variable is not a string");
    }
    out << destContext.asString();
  }
};

struct IfElseNode {
  std::string_view conditionVariable;
  TemplateDocument trueBranch;
  TemplateDocument falseBranch;

  void render(const JinjaContext& context, std::ostream& out) const {
    const JinjaContext& condContext =
        SubstitutionNode::get_destination_context(context, conditionVariable);
    bool condition = false;
    if (condContext.isString()) {
      condition = !condContext.asString().empty();
    } else if (condContext.isArray()) {
      condition = !condContext.asArray().empty();
    } else if (condContext.isObject()) {
      condition = !condContext.asObject().empty();
    }
    if (condition) {
      trueBranch.render(context, out);
    } else {
      falseBranch.render(context, out);
    }
  }
};

struct ForEachNode {
  std::string_view loopVariable;
  std::string_view itemVariable;
  TemplateDocument body;

  void render(const JinjaContext& context, std::ostream& out) const {
    const JinjaContext& loopContext =
        SubstitutionNode::get_destination_context(context, loopVariable);
    if (!loopContext.isArray()) {
      throw std::runtime_error("For loop variable is not an array");
    }
    const auto& arr = loopContext.asArray();
    std::string itemVarStr(itemVariable);
    for (const JinjaContext& item : arr) {
      JinjaObject loopBodyContextMap = context.asObject();
      loopBodyContextMap.erase(itemVarStr);
      loopBodyContextMap.emplace(itemVarStr, item);
      JinjaContext loopBodyContext{std::move(loopBodyContextMap)};
      body.render(loopBodyContext, out);
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

auto find_matching_token(std::span<const Token> tokens, Token::Type startType, Token::Type needle,
                         Token::Type endType) -> std::size_t {
  int nestedCount = 0;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].type == startType) {
      ++nestedCount;
    } else if (tokens[i].type == needle && nestedCount == 0) {
      return i;
    } else if (tokens[i].type == endType) {
      if (nestedCount == 0) {
        return tokens.size();
      } else {
        --nestedCount;
      }
    }
  }
  throw std::runtime_error("No matching end token found");
}

auto parse_if_else(std::span<const Token> tokens) -> ParserResult {
  assert(tokens[0].type == Token::Type::If);
  if (tokens.size() < 2 || tokens[1].type != Token::Type::Identifier) {
    throw std::runtime_error("Expected identifier after 'if'");
  }
  if (tokens.size() < 3) {
    throw std::runtime_error("Unexpected end of tokens after 'if' condition");
  }
  if (tokens[2].type != Token::Type::BlockEnd) {
    throw std::runtime_error("Expected block end after 'if' condition");
  }
  std::string_view conditionVar = tokens[1].value;
  tokens = tokens.subspan(3); // Skip If, condition variable, and BlockEnd

  std::size_t index =
      find_matching_token(tokens, Token::Type::If, Token::Type::EndIf, Token::Type::EndIf);
  if (index == tokens.size()) {
    throw std::runtime_error("Expected 'endif' for 'if' block");
  }
  assert(tokens[index].type == Token::Type::EndIf);
  assert(index > 0); // There should be at least one token between If and End
  if (index + 1 >= tokens.size()) {
    throw std::runtime_error("Unexpected end of tokens after 'if' block");
  }
  if (tokens[index - 1].type != Token::Type::BlockStart) {
    throw std::runtime_error("Expected block start before 'endif'");
  }
  if (tokens[index + 1].type != Token::Type::BlockEnd) {
    throw std::runtime_error("Expected block end after 'endif'");
  }
  std::span<const Token> ifClauseTokens =
      tokens.subspan(0, index - 1); // Exclude BlockStart before EndIf
  std::size_t elseIndex =
      find_matching_token(ifClauseTokens, Token::Type::If, Token::Type::Else, Token::Type::EndIf);
  if (elseIndex == 0) {
    throw std::runtime_error("Unexpected 'else' at the beginning of 'if' block");
  }
  TemplateDocument trueBranch = TemplateDocument{EmptyDocument{}};
  TemplateDocument falseBranch = TemplateDocument{EmptyDocument{}};
  if (elseIndex == ifClauseTokens.size()) {
    trueBranch = make_document(ifClauseTokens);
  } else {
    if (ifClauseTokens[elseIndex - 1].type != Token::Type::BlockStart) {
      throw std::runtime_error("Expected block start before 'else'");
    }
    if (elseIndex + 1 >= ifClauseTokens.size() ||
        ifClauseTokens[elseIndex + 1].type != Token::Type::BlockEnd) {
      throw std::runtime_error("Expected block end after 'else'");
    }
    std::span<const Token> trueTokens =
        ifClauseTokens.subspan(0, elseIndex - 1); // Exclude BlockStart before Else
    std::span<const Token> falseTokens =
        ifClauseTokens.subspan(elseIndex + 2); // Skip Else and BlockEnd
    trueBranch = make_document(trueTokens);
    falseBranch = make_document(falseTokens);
  }
  return ParserResult{
      TemplateDocument{IfElseNode{conditionVar, std::move(trueBranch), std::move(falseBranch)}},
      tokens.subspan(index + 2) // Skip past EndIf and BlockEnd
  };
}

auto parse_for_each(std::span<const Token> tokens) -> ParserResult {
  assert(tokens[0].type == Token::Type::For);
  if (tokens.size() < 4 || tokens[1].type != Token::Type::Identifier ||
      tokens[2].type != Token::Type::In || tokens[3].type != Token::Type::Identifier) {
    throw std::runtime_error("Expected 'for <item> in <array>' syntax");
  }
  std::string_view itemVar = tokens[1].value;
  std::string_view loopVar = tokens[3].value;
  if (tokens.size() < 5 || tokens[4].type != Token::Type::BlockEnd) {
    throw std::runtime_error("Unexpected end of tokens after 'for' declaration");
  }
  tokens = tokens.subspan(5); // Skip For, item variable, In, loop variable, and BlockEnd

  std::size_t index =
      find_matching_token(tokens, Token::Type::For, Token::Type::EndFor, Token::Type::EndFor);
  if (index == tokens.size()) {
    throw std::runtime_error("Expected 'endfor' for 'for' block");
  }
  std::span<const Token> forBodyTokens = tokens.subspan(0, index);
  TemplateDocument body = make_document(forBodyTokens);
  return ParserResult{
      TemplateDocument{ForEachNode{loopVar, itemVar, body}},
      tokens.subspan(index + 1) // Skip past EndFor
  };
}

auto parse_block(std::span<const Token> tokens) -> ParserResult {
  tokens = tokens.subspan(1); // Skip BlockStart
  if (tokens.empty()) {
    throw std::runtime_error("Unexpected end of tokens in block");
  }
  if (tokens[0].type == Token::Type::If) {
    return parse_if_else(tokens);
  } else if (tokens[0].type == Token::Type::For) {
    return parse_for_each(tokens);
  }
  throw std::runtime_error("Unsupported block type");
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
    return ParserResult{TemplateDocument{EmptyDocument{}}, std::span<const Token>{}};
  case Token::Type::VariableEnd:
    [[fallthrough]];
  case Token::Type::BlockEnd:
    [[fallthrough]];
  case Token::Type::If:
    [[fallthrough]];
  case Token::Type::Else:
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

auto make_document(std::span<const Token> tokens) -> TemplateDocument {
  std::vector<TemplateDocument> documents;
  ParserResult result = parse_next_document(tokens);
  documents.push_back(std::move(result.document));
  while (!result.remainingTokens.empty()) {
    result = parse_next_document(result.remainingTokens);
    documents.push_back(std::move(result.document));
  }
  return TemplateDocument{MultipleNodes{documents}};
}

} // namespace

auto make_document(std::string_view templateContent) -> TemplateDocument {
  auto tokens = tokenize(templateContent);
  return make_document(tokens);
}

} // namespace ms