// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "JinjaTemplateEngine.hpp"

#include <algorithm>
#include <cassert>
#include <format>
#include <map>
#include <span>
#include <stdexcept>

namespace ms {
MapObject::MapObject() = default;

MapObject::MapObject(std::map<std::string, JinjaContext> map) : mMap(std::move(map)) {}

auto MapObject::find(const std::string& key) const -> const JinjaContext* {
  auto it = mMap.find(key);
  if (it != mMap.end()) {
    return &it->second;
  }
  return nullptr;
}

auto JinjaObject::find(const std::string& key) const -> const JinjaContext* {
  return mFindFunc(mObject, key);
}

JinjaObject::JinjaObject(std::map<std::string, JinjaContext>&& map)
    : JinjaObject(MapObject{std::move(map)}) {}

JinjaObject::JinjaObject(const std::map<std::string, JinjaContext>& map)
    : JinjaObject(MapObject{map}) {}

class ContextObject {
public:
  ContextObject() = default;
  explicit ContextObject(std::map<std::string, JinjaContext> map, const JinjaObject* base)
      : mMap(std::move(map)), base(base) {}

  auto find(const std::string& key) const -> const JinjaContext* {
    auto it = mMap.find(key);
    if (it != mMap.end()) {
      return &it->second;
    }
    return base->find(key);
  }

private:
  std::map<std::string, JinjaContext> mMap;
  const JinjaObject* base;
};

JinjaContext::JinjaContext(const std::string& string) : mStorage(string) {}
JinjaContext::JinjaContext(std::string&& string) : mStorage(std::move(string)) {}

JinjaContext::JinjaContext(const JinjaObject& object) : mStorage(object) {}
JinjaContext::JinjaContext(JinjaObject&& object) : mStorage(std::move(object)) {}

JinjaContext::JinjaContext(const JinjaArray& array) : mStorage(array) {}
JinjaContext::JinjaContext(JinjaArray&& array) : mStorage(std::move(array)) {}

auto JinjaContext::isString() const noexcept -> bool { return mStorage.index() == 0; }

auto JinjaContext::isObject() const noexcept -> bool { return mStorage.index() == 1; }

auto JinjaContext::isArray() const noexcept -> bool { return mStorage.index() == 2; }

auto JinjaContext::asString() const -> const std::string& { return std::get<0>(mStorage); }

auto JinjaContext::asObject() const -> const JinjaObject& { return std::get<1>(mStorage); }

auto JinjaContext::asArray() const -> const JinjaArray& { return std::get<2>(mStorage); }

auto JinjaContext::asString() -> std::string& { return std::get<0>(mStorage); }

auto JinjaContext::asObject() -> JinjaObject& { return std::get<1>(mStorage); }

auto JinjaContext::asArray() -> JinjaArray& { return std::get<2>(mStorage); }

RenderError::RenderError(const std::string& message, Location loc, Location endLoc)
    : std::runtime_error(message), mStartLocation(loc), mEndLocation(endLoc) {}

auto RenderError::formatted_message(std::string_view content, const std::string& templateName) const
    -> std::string {
  std::string_view lineStart = content;
  for (std::size_t i = 0; i < mStartLocation.line - 1; ++i) {
    std::size_t nextLinePos = lineStart.find('\n');
    if (nextLinePos == std::string_view::npos) {
      break;
    }
    lineStart = lineStart.substr(nextLinePos + 1);
  }
  std::size_t lineEndPos = lineStart.find('\n');
  std::string_view line;
  if (lineEndPos != std::string_view::npos) {
    line = lineStart.substr(0, lineEndPos);
  } else {
    line = lineStart;
  }

  std::string underline = std::string(mStartLocation.column - 1, ' ');
  if (mEndLocation.line == mStartLocation.line && mEndLocation.column > mStartLocation.column) {
    underline += std::string(mEndLocation.column - mStartLocation.column, '^');
  } else {
    underline += '^';
  }
  return std::format("{}:{}: {}\n{}\n{}", templateName, mStartLocation.line, what(), line,
                     underline);
}

void TemplateDocument::render(const JinjaContext& context, std::ostream& out) const {
  if (mRenderFunc) {
    mRenderFunc(mDocument, context, out);
  }
}

namespace {

class TemplateError : public std::runtime_error {
public:
  TemplateError(const std::string& message, Location location, Location endLocation = Location{});

  auto location() const noexcept -> Location;
  auto format_message(std::string_view templateName, std::string_view content) const -> std::string;

private:
  Location mLocationStart;
  Location mLocationEnd;
  std::string mMessage;
};

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
    EndOfFile
  };
  Type type;
  std::string_view value;
  Location location;
};

auto offset(Location location, std::ptrdiff_t offset) -> Location {
  return Location{location.line,
                  static_cast<std::size_t>(static_cast<std::ptrdiff_t>(location.column) + offset)};
}

template <class I> auto make_signed(I value) -> std::make_signed_t<I> {
  return static_cast<std::make_signed_t<I>>(value);
}

auto make_document(std::span<const Token> tokens) -> TemplateDocument;

struct Lexer {
  std::string_view mInput;
  std::size_t mPosition = 0;
  std::size_t mLine = 1;
  std::size_t mColumn = 1;

  auto validate_identifier(std::string_view identifier) -> std::string_view {
    if (identifier.empty() ||
        (!std::isalpha(static_cast<unsigned char>(identifier[0])) && identifier[0] != '_')) {
      throw TemplateError("Invalid identifier: '" + std::string(identifier) + "'", location());
    }

    std::string_view remaining = identifier;
    while (!remaining.empty()) {
      auto pointOrArrayPos = remaining.find_first_of(".[");
      std::string_view part = remaining.substr(0, pointOrArrayPos);
      if (part.empty() || !std::all_of(part.begin() + 1, part.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
          })) {
        throw TemplateError("Invalid identifier: '" + std::string(identifier) + "'", location());
      }
      remaining = remaining.substr(part.size());
      if (remaining.starts_with(".")) {
        if (remaining.size() == 1) {
          throw TemplateError("Trailing dot in identifier: '" + std::string(identifier) + "'",
                              offset(location(), make_signed(pointOrArrayPos)));
        }
        remaining = remaining.substr(1);
      } else if (remaining.starts_with("[")) {
        std::size_t closingBracket = remaining.find(']');
        if (closingBracket == std::string_view::npos) {
          throw TemplateError("Unterminated array index in identifier: '" +
                                  std::string(identifier) + "'",
                              offset(location(), make_signed(pointOrArrayPos)));
        }
        std::string_view indexPart = remaining.substr(1, closingBracket - 1);
        if (indexPart.empty() || !std::all_of(indexPart.begin(), indexPart.end(), [](char c) {
              return std::isdigit(static_cast<unsigned char>(c));
            })) {
          throw TemplateError("Invalid array index in identifier: '" + std::string(identifier) +
                                  "'",
                              offset(location(), make_signed(pointOrArrayPos + 1)),
                              offset(location(), make_signed(pointOrArrayPos + closingBracket)));
        }
        remaining = remaining.substr(closingBracket + 1);
        if (remaining.starts_with(".")) {
          if (remaining.size() == 1) {
            throw TemplateError("Trailing dot in identifier: '" + std::string(identifier) + "'",
                                offset(location(), make_signed(pointOrArrayPos)));
          }
          remaining = remaining.substr(1);
        }
      }
    }
    return identifier;
  }

  void advance(std::size_t count = 1) {
    for (std::size_t i = 0; i < count; ++i) {
      if (mPosition < mInput.size()) {
        if (mInput[mPosition] == '\n') {
          ++mLine;
          mColumn = 1;
        } else {
          ++mColumn;
        }
        ++mPosition;
      }
    }
  }

  void skip_whitespace() {
    while (mPosition < mInput.size() && std::isspace(mInput[mPosition])) {
      advance();
    }
  }

  static constexpr std::string_view ifLiteral = "if";
  static constexpr std::string_view elseLiteral = "else";
  static constexpr std::string_view endifLiteral = "endif";
  static constexpr std::string_view forLiteral = "for";
  static constexpr std::string_view endforLiteral = "endfor";
  static constexpr std::string_view inLiteral = "in";

  auto makeToken(Token::Type type, std::string_view value) -> Token {
    return Token{type, value, Location{mLine, mColumn}};
  }

  auto location() const noexcept -> Location { return Location{mLine, mColumn}; }

  auto nextWord(std::string_view remaining) -> std::string_view {
    auto space = remaining.find_first_of(" \t\r\n%}");
    return remaining.substr(0, space);
  }

  auto tokenize() -> std::vector<Token> {
    std::vector<Token> tokens;
    while (mPosition < mInput.size()) {
      if (mInput.substr(mPosition).starts_with("{{")) {
        const Location varStartLocation = location();
        tokens.push_back(makeToken(Token::Type::VariableStart, "{{"));
        advance(2);
        skip_whitespace();
        std::string_view remaining = mInput.substr(mPosition);
        std::size_t identifierEnd = remaining.find_first_of(" \t\r}");
        if (identifierEnd == std::string_view::npos) {
          throw TemplateError("Unterminated variable substitution", varStartLocation);
        }
        std::string_view identifier = validate_identifier(remaining.substr(0, identifierEnd));
        tokens.push_back(makeToken(Token::Type::Identifier, identifier));
        advance(identifierEnd);
        skip_whitespace();
        if (mInput.substr(mPosition).starts_with("}}")) {
          tokens.push_back(makeToken(Token::Type::VariableEnd, "}}"));
          advance(2);
        } else {
          throw TemplateError("Expected '}}' at the end of variable substitution",
                              varStartLocation);
        }
      } else if (mInput.substr(mPosition).starts_with("{%")) {
        const Location blockStartLocation = location();
        tokens.push_back(makeToken(Token::Type::BlockStart, "{%"));
        advance(2);
        skip_whitespace();
        std::string_view remaining = mInput.substr(mPosition);
        while (!remaining.empty() && !remaining.starts_with("%}")) {
          auto word = nextWord(remaining);
          if (word == ifLiteral) {
            tokens.push_back(makeToken(Token::Type::If, ifLiteral));
            advance(ifLiteral.size());
          } else if (word == elseLiteral) {
            tokens.push_back(makeToken(Token::Type::Else, elseLiteral));
            advance(elseLiteral.size());
          } else if (word == endifLiteral) {
            tokens.push_back(makeToken(Token::Type::EndIf, endifLiteral));
            advance(endifLiteral.size());
          } else if (word == forLiteral) {
            tokens.push_back(makeToken(Token::Type::For, forLiteral));
            advance(forLiteral.size());
          } else if (word == endforLiteral) {
            tokens.push_back(makeToken(Token::Type::EndFor, endforLiteral));
            advance(endforLiteral.size());
          } else if (word == inLiteral) {
            tokens.push_back(makeToken(Token::Type::In, inLiteral));
            advance(inLiteral.size());
          } else {
            std::size_t identifierEnd = remaining.find_first_of(" \t\r\n%}");
            if (identifierEnd == std::string_view::npos) {
              throw TemplateError("Unterminated block", blockStartLocation);
            }
            std::string_view identifier = validate_identifier(remaining.substr(0, identifierEnd));
            tokens.push_back(makeToken(Token::Type::Identifier, identifier));
            advance(identifierEnd);
          }
          skip_whitespace();
          remaining = mInput.substr(mPosition);
        }
        if (mInput.substr(mPosition).starts_with("%}")) {
          tokens.push_back(makeToken(Token::Type::BlockEnd, "%}"));
          advance(2);
        } else {
          throw TemplateError("Expected '%}' at the end of block", blockStartLocation);
        }
      } else {
        std::size_t substitutionStart = mInput.find("{{", mPosition);
        std::size_t blockStart = mInput.find("{%", mPosition);
        std::size_t textEnd = std::min(substitutionStart, blockStart);
        if (textEnd == std::string_view::npos) {
          textEnd = mInput.size();
        }
        tokens.push_back(
            makeToken(Token::Type::Text, mInput.substr(mPosition, textEnd - mPosition)));
        advance(textEnd - mPosition);
      }
    }
    tokens.push_back(makeToken(Token::Type::EndOfFile, ""));
    return tokens;
  }
};

auto tokenize(std::string_view templateContent) -> std::vector<Token> {
  Lexer lexer{templateContent, 0};
  return lexer.tokenize();
}

struct TextNode {
  std::string content;
  Location location;

  void render(const JinjaContext& /* context */, std::ostream& out) const { out << content; }
};

struct EmptyDocument {
  void render(const JinjaContext& /* context */, std::ostream& /* out */) const {
    // Do nothing
  }
};

struct SubstitutionNode {
  std::string identifierPath;
  Location location;

  static auto get_next_identifier(std::string_view identifier) -> std::string_view {
    auto nextPos = identifier.find_first_of(".[");
    if (nextPos == std::string_view::npos) {
      return identifier;
    }
    return identifier.substr(0, nextPos);
  }

  static auto get_destination_context(const JinjaContext& context, std::string_view identifier,
                                      Location location) -> const JinjaContext& {
    std::size_t index = 0;
    std::string_view prevVar = identifier;
    const JinjaContext* currentContext = &context;
    while (!prevVar.empty()) {
      const JinjaObject* currentObject = &currentContext->asObject();
      std::string nextVar(get_next_identifier(prevVar));
      auto ctx = currentObject->find(nextVar);
      if (!ctx) {
        // auto suggestion = find_closest_match(nextVar, *currentObject);
        // if (suggestion == std::nullopt) {
        throw RenderError(std::format("Variable '{}' not found in context", nextVar),
                          offset(location, make_signed(index)),
                          offset(location, make_signed(index + nextVar.size())));
        // }
        // throw RenderError(std::format("Variable '{}' not found in context\nDid you mean '{}'?",
        //                               nextVar, *suggestion),
        //                   offset(location, make_signed(index)),
        //                   offset(location, make_signed(index + nextVar.size())));
      }
      const JinjaContext& nextContext = *ctx;
      if (nextVar.size() == prevVar.size()) {
        return nextContext;
      }
      prevVar = prevVar.substr(nextVar.size());
      index += nextVar.size();
      if (nextContext.isString()) {
        throw RenderError("Cannot access sub-property of a string variable",
                          offset(location, make_signed(index - nextVar.size())),
                          offset(location, make_signed(index)));
      } else if (nextContext.isObject()) {
        if (!prevVar.starts_with(".")) {
          throw RenderError("Expected '.' after object variable",
                            offset(location, make_signed(index + 1)));
        }
        prevVar = prevVar.substr(1);
        index += 1;
        currentContext = &nextContext;
      } else if (nextContext.isArray()) {
        const JinjaArray* currentArray = &nextContext.asArray();
        while (true) {
          if (!prevVar.starts_with("[")) {
            throw RenderError("Expected '[' after array variable",
                              offset(location, make_signed(index)));
          }
          std::size_t closingBracket = prevVar.find("]");
          if (closingBracket == std::string_view::npos) {
            throw RenderError("Expected closing ']' for array index",
                              offset(location, make_signed(index)));
          }
          std::string_view indexStr = prevVar.substr(1, closingBracket - 1);
          std::size_t arrayIndex = std::stoul(std::string(indexStr));
          if (arrayIndex >= currentArray->size()) {
            throw RenderError("Array index out of bounds", offset(location, make_signed(index)),
                              offset(location, make_signed(index + closingBracket + 1)));
          }
          const JinjaContext& arrayItem = (*currentArray)[arrayIndex];
          if (arrayItem.isObject()) {
            currentContext = &arrayItem;
            prevVar = prevVar.substr(closingBracket + 1);
            if (!prevVar.starts_with(".")) {
              throw RenderError("Expected '.' after object variable",
                                offset(location, make_signed(index + 1)));
            }
            prevVar = prevVar.substr(1);
            index += closingBracket + 2;
            break;
          } else if (arrayItem.isString()) {
            if (closingBracket + 1 != prevVar.size()) {
              throw RenderError("Cannot access sub-property of a string variable",
                                offset(location, make_signed(index + closingBracket + 1)));
            }
            return arrayItem;
          } else {
            assert(arrayItem.isArray());
            currentArray = &arrayItem.asArray();
            prevVar = prevVar.substr(closingBracket + 1);
            index += closingBracket + 1;
          }
        }
      } else {
        throw RenderError("Unsupported variable type", offset(location, make_signed(index)));
      }
    }
    return *currentContext;
  }

  void render(const JinjaContext& context, std::ostream& out) const {
    const JinjaContext& destContext = get_destination_context(context, identifierPath, location);
    if (!destContext.isString()) {
      throw RenderError("Substitution variable is not a string", location,
                        offset(location, make_signed(identifierPath.size())));
    }
    out << destContext.asString();
  }
};

struct IfElseNode {
  std::string conditionVariable;
  TemplateDocument trueBranch;
  TemplateDocument falseBranch;
  Location location;

  void render(const JinjaContext& context, std::ostream& out) const {
    bool condition = false;
    try {
      const JinjaContext& condContext =
          SubstitutionNode::get_destination_context(context, conditionVariable, location);
      if (condContext.isString()) {
        condition = !condContext.asString().empty();
      } else if (condContext.isArray()) {
        condition = !condContext.asArray().empty();
      } else if (condContext.isObject()) {
        condition = true;
      }
    } catch (...) {
      condition = false;
    }
    if (condition) {
      trueBranch.render(context, out);
    } else {
      falseBranch.render(context, out);
    }
  }
};

struct ForEachNode {
  std::string loopVariable;
  std::string itemVariable;
  TemplateDocument body;
  Location itemVarlocation;
  Location loopVarLocation;

  void render(const JinjaContext& context, std::ostream& out) const {
    const JinjaContext& loopContext =
        SubstitutionNode::get_destination_context(context, loopVariable, loopVarLocation);
    if (!loopContext.isArray()) {
      throw RenderError("For loop variable is not an array", loopVarLocation,
                        offset(loopVarLocation, make_signed(loopVariable.size())));
    }
    const auto& arr = loopContext.asArray();
    std::string itemVarStr(itemVariable);
    for (const JinjaContext& item : arr) {
      const JinjaObject& loopBodyContextBase = context.asObject();
      std::map<std::string, JinjaContext> loopBodyContextMap;
      loopBodyContextMap.emplace(itemVarStr, item);
      JinjaObject loopBodyContextObj{
          ContextObject{std::move(loopBodyContextMap), &loopBodyContextBase}};
      JinjaContext loopBodyContext{std::move(loopBodyContextObj)};
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
  throw TemplateError("No matching end token found", tokens[0].location);
}

auto parse_if_else(std::span<const Token> tokens) -> ParserResult {
  assert(tokens[0].type == Token::Type::If);
  const Location ifLocation = tokens[0].location;
  if (tokens.size() < 2 || tokens[1].type != Token::Type::Identifier) {
    throw TemplateError("Expected identifier after 'if'", ifLocation, offset(ifLocation, 2));
  }
  const Location conditionLocation = tokens[1].location;
  if (tokens.size() < 3) {
    throw TemplateError("Unexpected end of tokens after 'if' condition", ifLocation,
                        offset(ifLocation, 2));
  }
  if (tokens[2].type != Token::Type::BlockEnd) {
    throw TemplateError("Expected block end after 'if' condition", ifLocation,
                        offset(ifLocation, 2));
  }
  std::string_view conditionVar = tokens[1].value;
  tokens = tokens.subspan(3); // Skip If, condition variable, and BlockEnd

  std::size_t endifIndex =
      find_matching_token(tokens, Token::Type::If, Token::Type::EndIf, Token::Type::EndIf);
  if (endifIndex == tokens.size()) {
    throw TemplateError("Expected 'endif' for 'if' block", ifLocation);
  }
  assert(tokens[endifIndex].type == Token::Type::EndIf);
  assert(endifIndex > 0); // There should be at least one token between If and End
  if (endifIndex + 1 >= tokens.size()) {
    throw TemplateError("Unexpected end of tokens after 'if' block", ifLocation);
  }
  if (tokens[endifIndex - 1].type != Token::Type::BlockStart) {
    throw TemplateError("Expected block start before 'endif'", ifLocation);
  }
  if (tokens[endifIndex + 1].type != Token::Type::BlockEnd) {
    throw TemplateError("Expected block end after 'endif'", ifLocation);
  }
  std::span<const Token> ifClauseTokens = tokens.subspan(0, endifIndex - 1);
  std::size_t elseIndex = find_matching_token(tokens.subspan(0, endifIndex + 1), Token::Type::If,
                                              Token::Type::Else, Token::Type::EndIf);
  if (elseIndex == 0) {
    throw TemplateError("Unexpected 'else' at the beginning of 'if' block", ifLocation);
  }
  TemplateDocument trueBranch = TemplateDocument{EmptyDocument{}};
  TemplateDocument falseBranch = TemplateDocument{EmptyDocument{}};
  if (elseIndex >= endifIndex) {
    trueBranch = make_document(ifClauseTokens);
  } else {
    if (ifClauseTokens[elseIndex - 1].type != Token::Type::BlockStart) {
      throw TemplateError("Expected block start before 'else'",
                          ifClauseTokens[elseIndex - 1].location);
    }
    if (elseIndex + 1 >= ifClauseTokens.size() ||
        ifClauseTokens[elseIndex + 1].type != Token::Type::BlockEnd) {
      throw TemplateError("Expected block end after 'else'",
                          ifClauseTokens[elseIndex - 1].location);
    }
    std::span<const Token> trueTokens =
        ifClauseTokens.subspan(0, elseIndex - 1); // Exclude BlockStart before Else
    std::span<const Token> falseTokens =
        ifClauseTokens.subspan(elseIndex + 2); // Skip Else and BlockEnd
    trueBranch = make_document(trueTokens);
    falseBranch = make_document(falseTokens);
  }
  return ParserResult{
      TemplateDocument{IfElseNode{std::string{conditionVar}, std::move(trueBranch),
                                  std::move(falseBranch), conditionLocation}},
      tokens.subspan(endifIndex + 2) // Skip past EndIf and BlockEnd
  };
}

auto parse_for_each(std::span<const Token> tokens) -> ParserResult {
  const Location forLocation = tokens[0].location;
  assert(tokens[0].type == Token::Type::For);
  if (tokens.size() < 4 || tokens[1].type != Token::Type::Identifier ||
      tokens[2].type != Token::Type::In || tokens[3].type != Token::Type::Identifier) {
    throw TemplateError("Expected 'for <item> in <array>' syntax", forLocation);
  }
  const Location itemVarLocation = tokens[1].location;
  const Location loopVarLocation = tokens[3].location;
  std::string_view itemVar = tokens[1].value;
  std::string_view loopVar = tokens[3].value;
  if (tokens.size() < 5 || tokens[4].type != Token::Type::BlockEnd) {
    throw TemplateError("Unexpected end of tokens after 'for' declaration", forLocation);
  }
  tokens = tokens.subspan(5); // Skip For, item variable, In, loop variable, and BlockEnd

  std::size_t index =
      find_matching_token(tokens, Token::Type::For, Token::Type::EndFor, Token::Type::EndFor);
  if (index == tokens.size()) {
    throw TemplateError("Expected 'endfor' for 'for' block", forLocation);
  }
  assert(index > 0); // There should be at least one token between For and EndFor
  assert(tokens[index].type == Token::Type::EndFor);
  if (tokens[index - 1].type != Token::Type::BlockStart) {
    throw TemplateError("Expected block start before 'endfor'", tokens[index].location);
  }
  if (index + 1 >= tokens.size() || tokens[index + 1].type != Token::Type::BlockEnd) {
    throw TemplateError("Expected block end after 'endfor'", tokens[index].location);
  }
  std::span<const Token> forBodyTokens =
      tokens.subspan(0, index - 1); // Exclude BlockStart before EndFor
  TemplateDocument body = make_document(forBodyTokens);
  ForEachNode node{std::string{loopVar}, std::string{itemVar}, std::move(body), {}, {}};
  node.itemVarlocation = itemVarLocation;
  node.loopVarLocation = loopVarLocation;
  return ParserResult{
      TemplateDocument{std::move(node)},
      tokens.subspan(index + 2) // Skip past EndFor and BlockEnd
  };
}

auto parse_block(std::span<const Token> tokens) -> ParserResult {
  const Location blockLocation = tokens[0].location;
  if (tokens.size() <= 1) {
    throw TemplateError("Unexpected end of tokens in block", blockLocation);
  }
  tokens = tokens.subspan(1); // Skip BlockStart
  if (tokens[0].type == Token::Type::If) {
    return parse_if_else(tokens);
  } else if (tokens[0].type == Token::Type::For) {
    return parse_for_each(tokens);
  }
  throw TemplateError("Unsupported block type", blockLocation);
}

auto parse_substitution(std::span<const Token> tokens) -> ParserResult {
  const Location substitutionLocation = tokens[0].location;
  assert(tokens[0].type == Token::Type::VariableStart);
  if (tokens.size() < 3) {
    throw TemplateError("Unexpected end of tokens in substitution", substitutionLocation);
  }
  if (tokens[1].type != Token::Type::Identifier) {
    throw TemplateError("Expected identifier in substitution", substitutionLocation);
  }
  if (tokens[2].type != Token::Type::VariableEnd) {
    throw TemplateError("Expected variable end token in substitution", substitutionLocation);
  }
  return ParserResult{
      TemplateDocument{SubstitutionNode{std::string{tokens[1].value}, tokens[1].location}},
      tokens.subspan(3)};
}

auto parse_next_document(std::span<const Token> tokens) -> ParserResult {
  if (tokens.empty()) {
    return ParserResult{TemplateDocument{EmptyDocument{}}, tokens};
  }
  switch (tokens[0].type) {
  case Token::Type::Text:
    return ParserResult{
        TemplateDocument{TextNode{std::string{tokens[0].value}, tokens[0].location}},
        tokens.subspan(1)};
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
    throw TemplateError("Unexpected token type in template", tokens[0].location);
  }

  throw TemplateError("Unsupported token type in template", tokens[0].location);
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

TemplateError::TemplateError(const std::string& message, Location location, Location endLocation)
    : std::runtime_error(message), mLocationStart(location), mLocationEnd(endLocation) {}

auto TemplateError::location() const noexcept -> Location { return mLocationStart; }

auto TemplateError::format_message(std::string_view templateName, std::string_view content) const
    -> std::string {
  if (templateName.empty()) {
    templateName = "<template>";
  }
  std::string formattedMessage = std::format("Error: {}:{}: {}\n", templateName,
                                             mLocationStart.line, std::runtime_error::what());

  // Extract the relevant line from the source content
  std::string_view remainingSource = content;
  std::size_t newLinePos = remainingSource.find('\n');
  for (std::size_t currentLine = 1;
       currentLine < mLocationStart.line && newLinePos != std::string_view::npos; ++currentLine) {
    remainingSource = remainingSource.substr(newLinePos + 1);
    newLinePos = remainingSource.find('\n');
  }
  if (remainingSource.empty()) {
    return formattedMessage;
  }
  newLinePos = remainingSource.find('\n');
  std::string_view errorLine = remainingSource.substr(0, newLinePos);
  formattedMessage += std::string(errorLine) + "\n";

  // Add indicator for the error column
  formattedMessage += std::string(mLocationStart.column - 1, ' ');
  if (mLocationEnd.line == mLocationStart.line && mLocationEnd.column > mLocationStart.column) {
    formattedMessage += std::string(mLocationEnd.column - mLocationStart.column, '^');
  } else {
    formattedMessage += "^";
  }
  formattedMessage += "\n";

  return formattedMessage;
}

} // namespace

auto make_document(std::string_view templateContent, const std::string& templateName)
    -> TemplateDocument try {
  auto tokens = tokenize(templateContent);
  return make_document(tokens);
} catch (const TemplateError& e) {
  std::string message = e.format_message(templateName, templateContent);
  throw std::runtime_error(message);
}

} // namespace ms