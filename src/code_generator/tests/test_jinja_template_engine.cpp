#include "JinjaTemplateEngine.hpp"

#include <cassert>
#include <iostream>
#include <sstream>

void test_substitution_hello_world() {
  const std::string templateContent = "Hello, {{ name }}!";
  ms::TemplateDocument document = ms::make_document(templateContent);

  ms::JinjaContext context{ms::JinjaObject{{"name", ms::JinjaContext("World")}}};

  std::ostringstream output;
  document.render(context, output);

  assert(output.str() == "Hello, World!");
}

void test_substitution_nested_object() {
  const std::string templateContent = "User: {{ user.name }}, Age: {{ user.age }}";
  ms::TemplateDocument document = ms::make_document(templateContent);

  ms::JinjaContext context{ms::JinjaObject{
      {"user", ms::JinjaContext(ms::JinjaObject{{"name", ms::JinjaContext("Alice")},
                                                {"age", ms::JinjaContext("30")}})}}};

  std::ostringstream output;
  document.render(context, output);

  assert(output.str() == "User: Alice, Age: 30");
}

void test_if_else_statement() {
  const std::string templateContent =
      "{% if is_member %}Welcome back, member!{% else %}Please sign up.{% endif %}";
  ms::TemplateDocument document = ms::make_document(templateContent);

  // Test when is_member is true
  ms::JinjaContext contextTrue{ms::JinjaObject{{"is_member", ms::JinjaContext("true")}}};
  std::ostringstream outputTrue;
  document.render(contextTrue, outputTrue);
  std::string output = outputTrue.str();
  assert(output == "Welcome back, member!");

  // Test when is_member is false
  ms::JinjaContext contextFalse{ms::JinjaObject{{"is_member", ms::JinjaContext("")}}};
  std::ostringstream outputFalse;
  document.render(contextFalse, outputFalse);
  assert(outputFalse.str() == "Please sign up.");
}

void test_for_loop_statement() {
  const std::string templateContent = "Items:{% for item in items %} {{ item }}{% endfor %}";
  ms::TemplateDocument document = ms::make_document(templateContent);

  ms::JinjaContext context{ms::JinjaObject{
      {"items",
       ms::JinjaContext(ms::JinjaArray{ms::JinjaContext("Apple"), ms::JinjaContext("Banana"),
                                       ms::JinjaContext("Cherry")})}}};

  std::ostringstream output;
  document.render(context, output);

  assert(output.str() == "Items: Apple Banana Cherry");
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