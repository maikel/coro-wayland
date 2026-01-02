// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "JinjaTemplateEngine.hpp"
#include "WaylandXmlParser.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>

#include <getopt.h>

namespace ms {
struct ProgramOptions {
  std::filesystem::path pathToWaylandXml;
  std::filesystem::path templateDirectory;
  std::filesystem::path outputDirectory;
};

auto parse_command_line_args(int argc, char** argv) -> ProgramOptions;

auto read_full_file(std::filesystem::path file) -> std::string;

auto make_context(const XmlTag& protocol) -> JinjaContext;

} // namespace ms

int main(int argc, char** argv) {
  const ms::ProgramOptions programOptions = ms::parse_command_line_args(argc, argv);
  const std::string waylandContent = ms::read_full_file(programOptions.pathToWaylandXml);
  ms::XmlTag protocol = ms::parse_wayland_xml(waylandContent);
  const std::string templateContent(std::istreambuf_iterator<char>(std::cin),
                                    std::istreambuf_iterator<char>{});
  try {
    ms::JinjaContext context = make_context(protocol);
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
  static ::option long_options[] = {::option{"input", required_argument, nullptr, 'i'}, ::option{}};
  const char* short_options = "i:";
  int option_index = 0;
  int parsedShortOpt = ::getopt_long(argc, argv, short_options, long_options, &option_index);
  while (parsedShortOpt != -1) {
    switch (parsedShortOpt) {
    case 'i':
      if (optarg) {
        result.pathToWaylandXml = optarg;
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

auto make_subcontext(const XmlTag& tag) -> JinjaObject {
  JinjaObject root;
  for (const auto& [name, value] : tag.attributes) {
    root.emplace(name, value);
  }
  JinjaArray args;
  JinjaArray entries;
  for (const XmlNode& node : tag.children) {
    if (!node.isTag()) {
      continue;
    }
    const XmlTag& child = node.asTag();
    JinjaObject childObj;
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
        root.emplace("return_type", interface->second);
      } else {
        if (type != childObj.end() && interface != childObj.end() &&
            type->second.asString() == "object") {
          childObj.erase(type);
          childObj.emplace("type", "const " + interface->second.asString() + "&");
        } else if (type != childObj.end() && type->second.asString() == "uint") {
          childObj.erase(type);
          childObj.emplace("type", "std::uint32_t");
        } else if (type != childObj.end() && type->second.asString() == "string") {
          childObj.erase(type);
          childObj.emplace("type", "std::string_view");
        }
        if (!args.empty()) {
          childObj.emplace("__tail", "true");
        }
        args.emplace_back(std::move(childObj));
      }
    } else if (child.name == "entry") {
      entries.emplace_back(std::move(childObj));
    }
  }
  root.emplace("args", std::move(args));
  root.emplace("entries", std::move(entries));
  return root;
}

auto make_context(const XmlTag& protocol) -> JinjaContext {
  JinjaObject root;
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
      JinjaObject interface;
      for (const auto& [name, value] : interfaceTag.attributes) {
        interface.emplace(name, value);
        JinjaArray requests;
        JinjaArray events;
        JinjaArray enums;
        for (const XmlNode& child : interfaceTag.children) {
          if (child.isText()) {
            continue;
          }
          const XmlTag& childTag = child.asTag();
          if (childTag.name == "request") {
            requests.emplace_back(make_subcontext(childTag));
          } else if (childTag.name == "event") {
            JinjaObject obj = make_subcontext(childTag);
            if (!events.empty()) {
              obj.emplace("__tail", "true");
            }
            events.emplace_back(std::move(obj));
          }
        }
        interface.emplace("requests", std::move(requests));
        interface.emplace("events", std::move(events));
        interface.emplace("enums", std::move(enums));
      }
      interfaces.emplace_back(std::move(interface));
    }
  }
  root.emplace("interfaces", std::move(interfaces));
  return JinjaContext(root);
}

} // namespace ms