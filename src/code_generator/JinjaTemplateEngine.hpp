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
  using StorageType = std::variant<        //
      std::string,                         //
      std::map<std::string, JinjaContext>, //
      std::vector<JinjaContext>>;

  StorageType mStorage;
};

template <class Document>
inline constexpr auto render_implementation = +[](
    const std::any& docAny, const JinjaContext& context, std::ostream& out) {
  const Document& doc = std::any_cast<const Document&>(docAny);
  doc.render(context, out);
};

struct TemplateDocument {
  TemplateDocument() = default;
  TemplateDocument(const TemplateDocument&) = default;
  TemplateDocument(TemplateDocument&&) noexcept = default;
  TemplateDocument& operator=(const TemplateDocument&) = default;
  TemplateDocument& operator=(TemplateDocument&&) noexcept = default;
  ~TemplateDocument() = default;

  template <std::copyable Document>
  requires requires (const Document& doc, const JinjaContext& ctx, std::ostream& out) {
    { doc.render(ctx, out) } -> std::same_as<void>;
  }
  explicit TemplateDocument(Document doc);

  void render(const JinjaContext& context, std::ostream& out) const;

private:
  std::any mDocument;
  void (*mRenderFunc)(const std::any&, const JinjaContext&, std::ostream&);
};

auto make_document(std::string_view templateContent) -> TemplateDocument;

template <std::copyable Document>
requires requires (const Document& doc, const JinjaContext& ctx, std::ostream& out) {
  { doc.render(ctx, out) } -> std::same_as<void>;
}
TemplateDocument::TemplateDocument(Document doc)
    : mDocument(std::move(doc)), mRenderFunc(render_implementation<Document>) {}

} // namespace ms