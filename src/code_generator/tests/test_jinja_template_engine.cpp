#include "JinjaTemplateEngine.hpp"

#include <cassert>
#include <iostream>
#include <sstream>

void render(cw::TemplateDocument& document, const cw::JinjaContext& context, std::ostream& out,
            std::string_view content) {
  try {
    document.render(context, out);
  } catch (const cw::RenderError& e) {
    std::string message = e.formatted_message(content, "<template>");
    throw std::runtime_error(message);
  }
}

void test_substitution_hello_world() {
  const std::string templateContent = "Hello, {{ name }}!";
  cw::TemplateDocument document = cw::make_document(templateContent);

  cw::JinjaContext context{cw::JinjaObject{
      std::map<std::string, cw::JinjaContext>{{"name", cw::JinjaContext("World")}}}};

  std::ostringstream output;
  render(document, context, output, templateContent);

  assert(output.str() == "Hello, World!");
}

void test_substitution_nested_object() {
  const std::string templateContent = "User: {{ user.name }}, Age: {{ user.age }}";
  cw::TemplateDocument document = cw::make_document(templateContent);

  cw::JinjaContext context{cw::JinjaObject{std::map<std::string, cw::JinjaContext>{
      {"user", cw::JinjaContext(cw::JinjaObject{std::map<std::string, cw::JinjaContext>{
                   {"name", cw::JinjaContext("Alice")}, {"age", cw::JinjaContext("30")}}})}}}};

  std::ostringstream output;
  render(document, context, output, templateContent);

  assert(output.str() == "User: Alice, Age: 30");
}

void test_if_else_statement() {
  const std::string templateContent =
      "{% if is_member %}Welcome back, member!{% else %}Please sign up.{% endif %}";
  cw::TemplateDocument document = cw::make_document(templateContent);

  // Test when is_member is true
  cw::JinjaContext contextTrue{cw::JinjaObject{
      std::map<std::string, cw::JinjaContext>{{"is_member", cw::JinjaContext("true")}}}};
  std::ostringstream outputTrue;
  render(document, contextTrue, outputTrue, templateContent);
  std::string output = outputTrue.str();
  assert(output == "Welcome back, member!");

  // Test when is_member is false
  cw::JinjaContext contextFalse{cw::JinjaObject{
      std::map<std::string, cw::JinjaContext>{{"is_member", cw::JinjaContext("")}}}};
  std::ostringstream outputFalse;
  render(document, contextFalse, outputFalse, templateContent);
  assert(outputFalse.str() == "Please sign up.");
}

void test_for_loop_statement() {
  const std::string templateContent = "Items:{% for item in items %} {{ item }}{% endfor %}";
  cw::TemplateDocument document = cw::make_document(templateContent);

  cw::JinjaContext context{cw::JinjaObject{std::map<std::string, cw::JinjaContext>{
      {"items",
       cw::JinjaContext(cw::JinjaArray{cw::JinjaContext("Apple"), cw::JinjaContext("Banana"),
                                       cw::JinjaContext("Cherry")})}}}};

  std::ostringstream output;
  render(document, context, output, templateContent);

  assert(output.str() == "Items: Apple Banana Cherry");
}

void test_missing_variable() {
  const std::string templateContent = "{{ missin }}";
  cw::TemplateDocument doc = cw::make_document(templateContent);
  cw::JinjaContext ctx{cw::JinjaObject{
      std::map<std::string, cw::JinjaContext>{{"missing", cw::JinjaContext{"missing"}}}}};

  try {
    std::ostringstream out;
    doc.render(ctx, out);
    assert(false); // Should throw
  } catch (const cw::RenderError& e) {
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