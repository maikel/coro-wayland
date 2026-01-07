// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "JinjaTemplateEngine.hpp"
#include "WaylandXmlParser.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>

#include <getopt.h>

namespace ms {
struct ProgramOptions {
  std::filesystem::path pathToWaylandXml;
  std::string extension;
};

auto parse_command_line_args(int argc, char** argv) -> ProgramOptions;

auto read_full_file(std::filesystem::path file) -> std::string;

auto make_context(const XmlTag& protocol, std::string extension) -> JinjaContext;

} // namespace ms

int main(int argc, char** argv) {
  const ms::ProgramOptions programOptions = ms::parse_command_line_args(argc, argv);
  const std::string waylandContent = ms::read_full_file(programOptions.pathToWaylandXml);
  ms::XmlTag protocol = ms::parse_wayland_xml(waylandContent);
  const std::string templateContent(std::istreambuf_iterator<char>(std::cin),
                                    std::istreambuf_iterator<char>{});
  try {
    ms::JinjaContext context = make_context(protocol, programOptions.extension);
    ms::TemplateDocument document = ms::make_document(templateContent, "<stdin>");
    document.render(context, std::cout);
  } catch (const ms::RenderError& e) {
    std::cerr << "Error: " << e.formatted_message(templateContent, "<stdin>") << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
  }
}

namespace ms {

auto parse_command_line_args(int argc, char** argv) -> ProgramOptions {
  ProgramOptions result{};
  result.pathToWaylandXml = "/usr/share/wayland/wayland.xml";
  result.extension = "";
  static ::option long_options[] = {::option{"input", required_argument, nullptr, 'i'},
                                    ::option{"extension", required_argument, nullptr, 'e'},
                                    ::option{}};
  const char* short_options = "i:e:";
  int option_index = 0;
  int parsedShortOpt = ::getopt_long(argc, argv, short_options, long_options, &option_index);
  while (parsedShortOpt != -1) {
    switch (parsedShortOpt) {
    case 'i':
      if (optarg) {
        result.pathToWaylandXml = optarg;
      }
      break;
    case 'e':
      if (optarg) {
        result.extension = optarg;
      }
      break;
    default:
      std::cerr << "Unknown option '" << parsedShortOpt << "'\n";
      break;
    }
    parsedShortOpt = ::getopt_long(argc, argv, short_options, long_options, &option_index);
  }
  return result;
}

auto read_full_file(std::filesystem::path file) -> std::string {
  std::ifstream in(file.string());
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return content;
}

auto to_camel_case(std::string_view str) -> std::string {
  if (str.starts_with("wl_")) {
    str.remove_prefix(3);
  }
  std::string result;
  bool upperNext = true;
  for (char ch : str) {
    if (ch == '_') {
      upperNext = true;
    } else {
      if (upperNext) {
        result += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        upperNext = false;
      } else {
        result += ch;
      }
    }
  }
  return result;
}

auto make_subcontext(const XmlTag& tag) -> std::map<std::string, JinjaContext> {
  std::map<std::string, JinjaContext> root;
  for (const auto& [name, value] : tag.attributes) {
    root.emplace(name, value);
  }
  root.emplace("cppname", JinjaContext{to_camel_case(root.at("name").asString())});
  JinjaArray args;
  JinjaArray entries;
  std::size_t count = 0;
  bool isBind = root.at("name").asString() == "bind";
  for (const XmlNode& node : tag.children) {
    if (!node.isTag()) {
      continue;
    }
    const XmlTag& child = node.asTag();
    std::map<std::string, JinjaContext> childObj;
    if (count + 1 < tag.children.size()) {
      childObj.emplace("__not_last", "true");
    }
    for (const auto& [name, value] : child.attributes) {
      childObj.emplace(name, value);
    }
    if (child.name == "description") {
      for (const auto& text : child.children) {
        if (text.isText()) {
          root.emplace("description", text.asText());
        }
      }
    } else if (child.name == "arg") {
      auto type = childObj.find("type");
      auto interface = childObj.find("interface");
      if (type != childObj.end() && interface != childObj.end() &&
          type->second.asString() == "new_id") {
        root.emplace("return_type", to_camel_case(interface->second.asString()));
        ++count;
        continue;
      } else if (type != childObj.end() && interface == childObj.end() &&
                 type->second.asString() == "new_id") {
        if (isBind) {
          // In the wl_registry bind request we need to expand new_id to (string, version, ObjectId)
          std::map<std::string, JinjaContext> stringArg{{{"name", JinjaContext{"interface"}},
                                                         {"type", JinjaContext{"std::string"}},
                                                         {"__tail", JinjaContext{"true"}}}};
          std::map<std::string, JinjaContext> versionArg{{{"name", JinjaContext{"version"}},
                                                          {"type", JinjaContext{"std::uint32_t"}},
                                                          {"__tail", JinjaContext{"true"}}}};
          std::map<std::string, JinjaContext> objectIdArg{{{"name", JinjaContext{"new_id"}},
                                                           {"type", JinjaContext{"ObjectId"}},
                                                           {"__tail", JinjaContext{"true"}}}};
          args.emplace_back(JinjaObject{std::move(stringArg)});
          args.emplace_back(JinjaObject{std::move(versionArg)});
          args.emplace_back(JinjaObject{std::move(objectIdArg)});
          ++count;
          continue;
        } else {
          childObj.erase(type);
          childObj.emplace("type", "ObjectId");
        }
      } else if (type != childObj.end() && interface != childObj.end() &&
                 type->second.asString() == "object") {
        childObj.erase(type);
        childObj.emplace("type", to_camel_case(interface->second.asString()));
      } else if (type != childObj.end() && interface == childObj.end() &&
                 type->second.asString() == "object") {
        childObj.erase(type);
        childObj.emplace("type", "ObjectId");
      } else if (type != childObj.end() && type->second.asString() == "uint") {
        childObj.erase(type);
        childObj.emplace("type", "std::uint32_t");
      } else if (type != childObj.end() && type->second.asString() == "int") {
        childObj.erase(type);
        childObj.emplace("type", "std::int32_t");
      } else if (type != childObj.end() && type->second.asString() == "string") {
        childObj.erase(type);
        childObj.emplace("type", "std::string");
      } else if (type != childObj.end() && type->second.asString() == "array") {
        childObj.erase(type);
        childObj.emplace("type", "std::vector<char>");
      } else if (type != childObj.end() && type->second.asString() == "fixed") {
        childObj.erase(type);
        childObj.emplace("type", "std::uint32_t");
      } else if (type != childObj.end() && type->second.asString() == "fd") {
        childObj.erase(type);
        childObj.emplace("type", "FileDescriptorHandle");
      }
      if (!args.empty()) {
        childObj.emplace("__tail", "true");
      }
      args.emplace_back(JinjaObject{std::move(childObj)});
    } else if (child.name == "entry") {
      std::string& name = childObj.at("name").asString();
      if (std::isdigit(name[0])) {
        name = "k" + name;
      } else if (name == "default") {
        name = "kDefault";
      }
      entries.emplace_back(JinjaObject{std::move(childObj)});
    }
    ++count;
  }
  root.emplace("args", std::move(args));
  root.emplace("entries", std::move(entries));
  return root;
}

auto make_context(const XmlTag& protocol, std::string extension) -> JinjaContext {
  std::map<std::string, JinjaContext> root;
  root.emplace("extension", extension);
  for (const auto& [name, value] : protocol.attributes) {
    root.emplace(name, value);
  }
  JinjaArray interfaces;
  for (const XmlNode& node : protocol.children) {
    if (node.isTag()) {
      const XmlTag& interfaceTag = node.asTag();
      if (interfaceTag.name != "interface") {
        continue;
      }
      std::map<std::string, JinjaContext> interface;
      for (const auto& [name, value] : interfaceTag.attributes) {
        interface.emplace(name, value);
      }
      interface.emplace("cppname", JinjaContext{to_camel_case(interface.at("name").asString())});
      JinjaArray requests;
      JinjaArray events;
      JinjaArray enums;
      for (const XmlNode& child : interfaceTag.children) {
        if (child.isText()) {
          continue;
        }
        const XmlTag& childTag = child.asTag();
        if (childTag.name == "request") {
          auto map = make_subcontext(childTag);
          map.emplace("num", std::to_string(requests.size()));
          requests.emplace_back(JinjaObject{std::move(map)});
        } else if (childTag.name == "event") {
          std::map<std::string, JinjaContext> obj = make_subcontext(childTag);
          std::string name = obj.at("cppname").asString();
          name += "Event";
          obj.erase("cppname");
          obj.emplace("cppname", name);
          if (!events.empty()) {
            obj.emplace("__tail", "true");
          }
          obj.emplace("num", std::to_string(events.size()));
          events.emplace_back(JinjaObject{std::move(obj)});
        } else if (childTag.name == "enum") {
          enums.emplace_back(JinjaObject{make_subcontext(childTag)});
        }
      }
      interface.emplace("requests", std::move(requests));
      interface.emplace("events", std::move(events));
      interface.emplace("enums", std::move(enums));
      interfaces.emplace_back(JinjaObject{std::move(interface)});
    }
  }
  root.emplace("interfaces", std::move(interfaces));
  return JinjaContext(JinjaObject{std::move(root)});
}

} // namespace ms