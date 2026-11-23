// [enum.udecl] Example 2 (paragraph 4), docs/cxx-spec/dcl/enum.udecl.md
// needs: P4
// status: annotated

enum class fruit { orange, apple };
enum class color { red, orange };
void f() {
  using enum fruit;             // OK
  using enum color;             // error: color::orange and fruit::orange conflict // semantic
}
