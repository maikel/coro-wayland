// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <any>
#include <concepts>
#include <map>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ms {

class JinjaContext;

class JinjaObject {
public:
  JinjaObject() = default;
  JinjaObject(const JinjaObject&) = default;
  JinjaObject(JinjaObject&&) noexcept = default;
  JinjaObject& operator=(const JinjaObject&) = default;
  JinjaObject& operator=(JinjaObject&&) noexcept = default;
  ~JinjaObject() = default;

  JinjaObject(std::map<std::string, JinjaContext>&& map);
  JinjaObject(const std::map<std::string, JinjaContext>& map);

  template <class Object>
    requires(!std::same_as<Object, JinjaObject>) &&
            requires(const Object& obj, const std::string& key) {
              { obj.find(key) } -> std::convertible_to<const JinjaContext*>;
            }
  explicit JinjaObject(Object&& obj);

  auto find(const std::string& key) const -> const JinjaContext*;

private:
  std::any mObject;
  auto (*mFindFunc)(const std::any&, const std::string&) -> const JinjaContext*;
};

using JinjaArray = std::vector<JinjaContext>;

class JinjaContext {
public:
  explicit JinjaContext(const std::string& string);
  explicit JinjaContext(const JinjaObject& object);
  explicit JinjaContext(const JinjaArray& array);
  explicit JinjaContext(std::string&& string);
  explicit JinjaContext(JinjaObject&& object);
  explicit JinjaContext(JinjaArray&& array);

  auto isString() const noexcept -> bool;
  auto isObject() const noexcept -> bool;
  auto isArray() const noexcept -> bool;

  auto asString() const -> const std::string&;
  auto asObject() const -> const JinjaObject&;
  auto asArray() const -> const JinjaArray&;

  auto asString() -> std::string&;
  auto asObject() -> JinjaObject&;
  auto asArray() -> JinjaArray&;

  friend bool operator==(const JinjaContext&, const JinjaContext&) = default;

private:
  using StorageType = std::variant< //
      std::string,                  //
      JinjaObject,                  //
      JinjaArray>;

  StorageType mStorage;
};

template <class Document>
inline constexpr auto render_implementation =
    +[](const std::any& docAny, const JinjaContext& context, std::ostream& out) {
      const Document& doc = std::any_cast<const Document&>(docAny);
      doc.render(context, out);
    };

struct Location {
  std::size_t line;
  std::size_t column;
};

class RenderError : public std::runtime_error {
public:
  RenderError(const std::string& message, Location loc, Location endLoc = Location{});

  auto formatted_message(std::string_view content, const std::string& templateName) const
      -> std::string;

private:
  Location mStartLocation;
  Location mEndLocation;
};

struct TemplateDocument {
  TemplateDocument() = default;
  TemplateDocument(const TemplateDocument&) = default;
  TemplateDocument(TemplateDocument&&) noexcept = default;
  TemplateDocument& operator=(const TemplateDocument&) = default;
  TemplateDocument& operator=(TemplateDocument&&) noexcept = default;
  ~TemplateDocument() = default;

  template <class Document>
    requires(!std::same_as<Document, TemplateDocument>) &&
            requires(const Document& doc, const JinjaContext& ctx, std::ostream& out) {
              { doc.render(ctx, out) } -> std::same_as<void>;
            }
  explicit TemplateDocument(Document doc);

  void render(const JinjaContext& context, std::ostream& out) const;

private:
  std::any mDocument;
  void (*mRenderFunc)(const std::any&, const JinjaContext&, std::ostream&);
};

auto make_document(std::string_view templateContent, const std::string& templateName = "")
    -> TemplateDocument;

class MapObject {
public:
  MapObject();
  explicit MapObject(std::map<std::string, JinjaContext> map);

  auto find(const std::string& key) const -> const JinjaContext*;

private:
  std::map<std::string, JinjaContext> mMap;
};

template <class Document>
  requires(!std::same_as<Document, TemplateDocument>) &&
              requires(const Document& doc, const JinjaContext& ctx, std::ostream& out) {
                { doc.render(ctx, out) } -> std::same_as<void>;
              }
TemplateDocument::TemplateDocument(Document doc)
    : mDocument(std::move(doc)), mRenderFunc(render_implementation<Document>) {}

template <class Object>
  requires(!std::same_as<Object, JinjaObject>) &&
              requires(const Object& obj, const std::string& key) {
                { obj.find(key) } -> std::convertible_to<const JinjaContext*>;
              }
JinjaObject::JinjaObject(Object&& obj)
    : mObject(std::forward<Object>(obj)),
      mFindFunc(+[](const std::any& anyObj, const std::string& key) -> const JinjaContext* {
        const Object& actualObj = std::any_cast<const Object&>(anyObj);
        return actualObj.find(key);
      }) {}

} // namespace ms