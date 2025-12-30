// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "JinjaTemplateEngine.hpp"
#include "WaylandXmlParser.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <getopt.h>

namespace ms {
struct ProgramOptions {
  std::filesystem::path pathToWaylandXml;
  std::filesystem::path templateDirectory;
  std::filesystem::path outputDirectory;
};

auto parse_command_line_args(int argc, char** argv) -> ProgramOptions;

auto find_all_template_files(std::filesystem::path dir) -> std::vector<std::filesystem::path>;

auto read_full_file(std::filesystem::path file) -> std::string;

auto make_path_to_generated_file(std::filesystem::path templatePath, const ProgramOptions& options)
    -> std::filesystem::path;

void print_context(const JinjaContext& context, std::ostream& out, const std::string& prefix = "");

} // namespace ms

int main(int argc, char** argv) {
  const ms::ProgramOptions programOptions = ms::parse_command_line_args(argc, argv);
  const std::string waylandContent = ms::read_full_file(programOptions.pathToWaylandXml);
  ms::JinjaContext context = ms::parse_wayland_xml(waylandContent);
  print_context(context, std::cout, "interfaces");
  // auto templates = ms::find_all_template_files(programOptions.templateDirectory);
  // for (auto templatePath : templates) {
  //   const std::string templateContent = ms::read_full_file(templatePath);
  //   const auto outputPath = ms::make_path_to_generated_file(templatePath, programOptions);
  //   std::ofstream out(outputPath.string());
  //   ms::generate_from_template(context, templateContent, out);
  // }
}

namespace ms {

auto parse_command_line_args(int argc, char** argv) -> ProgramOptions {
  ProgramOptions result{};
  result.pathToWaylandXml = "/usr/share/wayland/wayland.xml";
  result.outputDirectory = "./out";
  result.templateDirectory = "./templates";
  static ::option long_options[] = {::option{"out-dir", required_argument, nullptr, 0},
                                    ::option{"templates-dir", required_argument, nullptr, 0},
                                    ::option{"input", required_argument, nullptr, 0}, ::option{}};
  std::array<std::filesystem::path*, 3> paths = {&result.outputDirectory, &result.templateDirectory,
                                                 &result.pathToWaylandXml};
  const char* short_options = "o:i:d:";
  int option_index = 0;
  int parsedShortOpt = ::getopt_long(argc, argv, short_options, long_options, &option_index);
  std::size_t index = static_cast<std::size_t>(option_index);
  while (parsedShortOpt != -1) {
    switch (parsedShortOpt) {
    case 0:
      // long option
      if (index >= 0 && index < paths.size() && optarg) {
        *(paths[index]) = optarg;
      }
      break;
    case 'o':
      if (optarg) {
        result.outputDirectory = optarg;
      }
      break;
    case 'i':
      if (optarg) {
        result.pathToWaylandXml = optarg;
      }
      break;
    case 'd':
      if (optarg) {
        result.templateDirectory = optarg;
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

auto getName(const std::map<std::string, ms::JinjaContext>& object) -> std::string {
  auto it = object.find("name");
  if (it != object.end() && it->second.isString()) {
    return it->second.asString();
  }
  return {};
}

void print_context(const JinjaContext& context, std::ostream& out, const std::string& prefix)
{
  if (context.isString()) {
    out << prefix << " = " << context.asString() << "\n";
  } else if (context.isObject()) {
    const std::string name = prefix + "." + getName(context.asObject());
    // out << "Object " << name << ":\n";
    for (const auto& [key, value] : context.asObject()) {
      print_context(value, out, name + "." + key);
    }
  } else if (context.isArray()) {
    // out << "Array " << prefix << ":\n";
    int index = 0;
    for (const auto& item : context.asArray()) {
      print_context(item, out, prefix + "[" + std::to_string(index) + "]");
      ++index;
    }
  }
}

} // namespace ms