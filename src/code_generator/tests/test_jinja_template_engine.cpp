#include "JinjaTemplateEngine.hpp"

#include <cassert>
#include <iostream>
#include <sstream>

void render(ms::TemplateDocument& document, const ms::JinjaContext& context, std::ostream& out,
            std::string_view content) {
  try {
    document.render(context, out);
  } catch (const ms::RenderError& e) {
    std::string message = e.formatted_message(content, "<template>");
    throw std::runtime_error(message);
  }
}

void test_substitution_hello_world() {
  const std::string templateContent = "Hello, {{ name }}!";
  ms::TemplateDocument document = ms::make_document(templateContent);

  ms::JinjaContext context{ms::JinjaObject{
      std::map<std::string, ms::JinjaContext>{{"name", ms::JinjaContext("World")}}}};

  std::ostringstream output;
  render(document, context, output, templateContent);

  assert(output.str() == "Hello, World!");
}

void test_substitution_nested_object() {
  const std::string templateContent = "User: {{ user.name }}, Age: {{ user.age }}";
  ms::TemplateDocument document = ms::make_document(templateContent);

  ms::JinjaContext context{ms::JinjaObject{std::map<std::string, ms::JinjaContext>{
      {"user", ms::JinjaContext(ms::JinjaObject{std::map<std::string, ms::JinjaContext>{
                   {"name", ms::JinjaContext("Alice")}, {"age", ms::JinjaContext("30")}}})}}}};

  std::ostringstream output;
  render(document, context, output, templateContent);

  assert(output.str() == "User: Alice, Age: 30");
}

void test_if_else_statement() {
  const std::string templateContent =
      "{% if is_member %}Welcome back, member!{% else %}Please sign up.{% endif %}";
  ms::TemplateDocument document = ms::make_document(templateContent);

  // Test when is_member is true
  ms::JinjaContext contextTrue{ms::JinjaObject{
      std::map<std::string, ms::JinjaContext>{{"is_member", ms::JinjaContext("true")}}}};
  std::ostringstream outputTrue;
  render(document, contextTrue, outputTrue, templateContent);
  std::string output = outputTrue.str();
  assert(output == "Welcome back, member!");

  // Test when is_member is false
  ms::JinjaContext contextFalse{ms::JinjaObject{
      std::map<std::string, ms::JinjaContext>{{"is_member", ms::JinjaContext("")}}}};
  std::ostringstream outputFalse;
  render(document, contextFalse, outputFalse, templateContent);
  assert(outputFalse.str() == "Please sign up.");
}

void test_for_loop_statement() {
  const std::string templateContent = "Items:{% for item in items %} {{ item }}{% endfor %}";
  ms::TemplateDocument document = ms::make_document(templateContent);

  ms::JinjaContext context{ms::JinjaObject{std::map<std::string, ms::JinjaContext>{
      {"items",
       ms::JinjaContext(ms::JinjaArray{ms::JinjaContext("Apple"), ms::JinjaContext("Banana"),
                                       ms::JinjaContext("Cherry")})}}}};

  std::ostringstream output;
  render(document, context, output, templateContent);

  assert(output.str() == "Items: Apple Banana Cherry");
}

void test_missing_variable() {
  const std::string templateContent = "{{ missin }}";
  ms::TemplateDocument doc = ms::make_document(templateContent);
  ms::JinjaContext ctx{ms::JinjaObject{
      std::map<std::string, ms::JinjaContext>{{"missing", ms::JinjaContext{"missing"}}}}};

  try {
    std::ostringstream out;
    doc.render(ctx, out);
    assert(false); // Should throw
  } catch (const ms::RenderError& e) {
    std::string msg = e.formatted_message(templateContent, "<template>");
    assert(msg.find("Did you mean") != std::string::npos);
  }
}

int main() try {
  test_substitution_hello_world();
  test_substitution_nested_object();
  test_if_else_statement();
  test_for_loop_statement();
} catch (const std::exception& ex) {
  std::cerr << "Test failed with exception:\n" << ex.what() << "\n";
  return 1;
}