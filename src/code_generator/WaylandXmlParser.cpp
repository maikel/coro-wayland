// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "WaylandXmlParser.hpp"

#include <cctype>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace ms {

namespace {

struct Token {
  enum class Type {
    OpenTag,
    CloseTag,
    SelfClose,
    OpenCloseTag,
    TagName,
    AttributeName,
    AttributeValue,
    Text,
    Eof
  };
  Type type;
  std::string_view value;
};

struct Lexer {
  std::string_view mInput;
  std::size_t mPosition = 0;
  bool mInTag = false;

  void skip_whitespace() {
    while (mPosition < mInput.size() && std::isspace(mInput[mPosition])) {
      ++mPosition;
    }
  }

  void add_attribute_tokens(std::vector<Token>& tokens) {
    skip_whitespace();
    std::string_view remaining = mInput.substr(mPosition);
    while (!remaining.empty() && !remaining.starts_with(">") && !remaining.starts_with("/>")) {
      std::size_t nameEnd = remaining.find_first_of("= />");
      if (nameEnd == std::string_view::npos) {
        nameEnd = remaining.size();
      }
      std::string_view name = remaining.substr(0, nameEnd);
      if (!name.empty()) {
        tokens.push_back(Token{Token::Type::AttributeName, name});
        mPosition += nameEnd;
        skip_whitespace();
        remaining = mInput.substr(mPosition);
        if (remaining.starts_with("=")) {
          mPosition += 1; // Skip '='
          skip_whitespace();
          remaining = mInput.substr(mPosition);
          if (remaining.starts_with("\"")) {
            mPosition += 1; // Skip opening quote
            std::size_t valueEnd = remaining.find('"', 1);
            if (valueEnd == std::string_view::npos) {
              throw std::runtime_error("Unterminated attribute value");
            }
            std::string_view value = remaining.substr(1, valueEnd - 1);
            tokens.push_back(Token{Token::Type::AttributeValue, value});
            mPosition += valueEnd; // Move past the closing quote
            skip_whitespace();
            remaining = mInput.substr(mPosition);
          } else {
            throw std::runtime_error("Expected '\"' at the beginning of attribute value");
          }
        } else {
          throw std::runtime_error("Expected '=' after attribute name");
        }
      } else {
        break;
      }
    }
  }

  auto tokenize() -> std::vector<Token> {
    std::vector<Token> tokens;
    skip_whitespace();
    std::string_view remaining = mInput.substr(mPosition);
    while (!remaining.empty()) {
        if (!mInTag) {
            if (remaining.starts_with("<?")) {
                // Skip XML declaration
                std::size_t declEnd = remaining.find("?>");
                if (declEnd == std::string_view::npos) {
                    throw std::runtime_error("Unterminated XML declaration");
                }
                mPosition += declEnd + 2;
            } else if (remaining.starts_with("<!--")) {
                // Skip comments
                std::size_t commentEnd = remaining.find("-->");
                if (commentEnd == std::string_view::npos) {
                    throw std::runtime_error("Unterminated XML comment");
                }
                mPosition += commentEnd + 3; 
            } else if (remaining.starts_with("</")) {
                mInTag = true;
                tokens.push_back(Token{Token::Type::OpenCloseTag, "</"});
                mPosition += 2;
            } else if (remaining.starts_with("<")) {
                mInTag = true;
                tokens.push_back(Token{Token::Type::OpenTag, "<"});
                mPosition += 1;
            } else {
                std::size_t textEnd = remaining.find('<');
                if (textEnd == std::string_view::npos) {
                    textEnd = remaining.size();
                }
                tokens.push_back(Token{Token::Type::Text, remaining.substr(0, textEnd)});
                mPosition += textEnd;
            }
        } else {
            if (remaining.starts_with("/>")) {
                tokens.push_back(Token{Token::Type::SelfClose, "/>"});
                mPosition += 2;
                mInTag = false;
            } else if (remaining.starts_with(">")) {
                tokens.push_back(Token{Token::Type::CloseTag, ">"});
                mPosition += 1;
                mInTag = false;
            } else if (remaining.starts_with("</")) {
                throw std::runtime_error("Unexpected '</' inside tag");
            } else {
                remaining = mInput.substr(mPosition);
                std::size_t nameEnd = remaining.find_first_of(" />");
                if (nameEnd == std::string_view::npos) {
                    nameEnd = remaining.size();
                }
                std::string_view name = remaining.substr(0, nameEnd);
                if (!name.empty()) {
                    tokens.push_back(Token{Token::Type::TagName, name});
                    mPosition += nameEnd;
                }
                add_attribute_tokens(tokens);
            }
        }
        skip_whitespace();
        remaining = mInput.substr(mPosition);
    }
    tokens.push_back(Token{Token::Type::Eof, ""});
    return tokens;
  }
};

auto tokenize(std::string_view xmlContent) -> std::vector<Token>
{
    Lexer lexer{xmlContent, 0};
    return lexer.tokenize();
}

struct XmlNode {
  std::string name;
  std::vector<std::pair<std::string, std::string>> attributes;
  std::vector<XmlNode> children;
  std::string text;
};

auto parse_tag(std::span<const Token> tokens) -> std::span<const Token> {
  if (tokens.empty()) {
    throw std::runtime_error("Unexpected end of tokens while parsing tag");
  }
  if (tokens[0].type != Token::Type::OpenTag && tokens[0].type != Token::Type::OpenCloseTag) {
    throw std::runtime_error("Expected open tag token");
  }
  if (tokens.size() < 2 || tokens[1].type != Token::Type::TagName) {
    throw std::runtime_error("Expected tag name token");
  }
  std::size_t index = 2;
  while (index < tokens.size()) {
    const Token& token = tokens[index];
    if (token.type == Token::Type::AttributeName && tokens[0].type == Token::Type::OpenTag) {
      if (index + 1 >= tokens.size() || tokens[index + 1].type != Token::Type::AttributeValue) {
        throw std::runtime_error("Expected attribute value token after attribute name");
      }
      index += 2;
    } else if (token.type == Token::Type::SelfClose && tokens[0].type == Token::Type::OpenTag) {
      // Handle self-closing tag
      ++index;
      break;
    } else if (token.type == Token::Type::CloseTag) {
      // Handle close tag
      ++index;
      break;
    } else {
      throw std::runtime_error("Unexpected token type while parsing tag");
    }
  }
  if (tokens[index - 1].type != Token::Type::SelfClose &&
      tokens[index - 1].type != Token::Type::CloseTag) {
    throw std::runtime_error("Expected self-closing or closing tag");
  }
  return tokens.subspan(0, index);
}

struct ParseXmlResult {
  XmlNode root;
  std::span<const Token> remainingTokens;
};
auto parse_xml_node(std::span<const Token> tokens) -> ParseXmlResult {
  XmlNode root;
  auto tag = parse_tag(tokens);
  root.name = tag[1].value;
  for (std::size_t i = 2; i < tag.size(); ++i) {
    if (tag[i].type == Token::Type::AttributeName) {
      std::string attrName = std::string(tag[i].value);
      std::string attrValue = std::string(tag[i + 1].value);
      root.attributes.emplace_back(attrName, attrValue);
      ++i; // Skip attribute value
    }
  }
  tokens = tokens.subspan(tag.size());
  if (tag.back().type == Token::Type::SelfClose) {
    // Self-closing tag, no children or text
    return ParseXmlResult{root, tokens};
  }
  while (!tokens.empty() && tokens[0].type != Token::Type::OpenCloseTag) {
    if (tokens[0].type == Token::Type::OpenTag) {
      ParseXmlResult child = parse_xml_node(tokens);
      root.children.push_back(std::move(child.root));
      tokens = child.remainingTokens;
    } else if (tokens[0].type == Token::Type::Text) {
      root.text += std::string(tokens[0].value);
      tokens = tokens.subspan(1);
    } else {
      throw std::runtime_error("Unexpected token type while parsing XML");
    }
  }
  if (!tokens.empty() && tokens[0].type == Token::Type::OpenCloseTag) {
    auto closeTag = parse_tag(tokens);
    if (closeTag[1].value != root.name) {
      throw std::runtime_error("Mismatched close tag: expected </" + root.name + "> but found </" +
                               std::string(closeTag[1].value) + ">");
    }
    tokens = tokens.subspan(closeTag.size());
  } else {
    throw std::runtime_error("Expected close tag token");
  }
  return ParseXmlResult{root, tokens};
}

auto getChildrenArray(const XmlNode& parent, const std::string& childName)
    -> std::vector<JinjaContext> {
  std::vector<JinjaContext> children;
  for (const XmlNode& child : parent.children) {
    if (child.name != childName) {
      continue;
    }
    std::map<std::string, JinjaContext> childContext;
    for (const auto& [name, value] : child.attributes) {
      childContext.emplace(name, JinjaContext(value));
    }
    std::vector<JinjaContext> argsContext = getChildrenArray(child, "arg");
    if (!argsContext.empty()) {
      childContext.emplace("args", JinjaContext(argsContext));
    }
    std::string name = childContext.at("name").asString();
    children.push_back(JinjaContext(childContext));
  }
  return children;
}

} // namespace

auto parse_wayland_xml(std::string_view xmlContent) -> JinjaContext {
  auto tokens = tokenize(xmlContent);
  std::vector<JinjaContext> interfaces;
  XmlNode root = parse_xml_node(std::span<const Token>(tokens.data(), tokens.size())).root;
  if (root.name != "protocol") {
    throw std::runtime_error("Expected root element to be <protocol>");
  }
  for (const auto& child : root.children) {
    if (child.name != "interface") {
      continue;
    }
    const XmlNode& interfaceNode = child;
    std::map<std::string, JinjaContext> interfaceContext;
    for (const auto& [name, value] : interfaceNode.attributes) {
      interfaceContext.emplace(name, JinjaContext(value));
    }
    std::vector<JinjaContext> requestsContext = getChildrenArray(interfaceNode, "request");
    if (!requestsContext.empty()) {
      interfaceContext.emplace("requests", JinjaContext(requestsContext));
    }
    std::vector<JinjaContext> eventsContext = getChildrenArray(interfaceNode, "event");
    if (!eventsContext.empty()) {
      interfaceContext.emplace("events", JinjaContext(eventsContext));
    }
    std::vector<JinjaContext> enumsContext = getChildrenArray(interfaceNode, "enum");
    if (!enumsContext.empty()) {
      interfaceContext.emplace("enums", JinjaContext(enumsContext));
    }
    std::string name = interfaceContext.at("name").asString();
    interfaces.push_back(JinjaContext(interfaceContext));
  }
  JinjaContext context{interfaces};
  return context;
}

} // namespace ms