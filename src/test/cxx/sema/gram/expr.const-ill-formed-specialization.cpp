// RUN: %clang_cc1 -std=c++17 -fsyntax-only -verify %s
// [temp.inst]/1, [expr.const]/5.3: a call of a function template's
// specialization whose definition is ill-formed for its arguments is not a
// constant expression (clang marks the specialization invalid).
template <typename T>
constexpr int g() {
    T *ptr; // expected-error {{'ptr' declared as a pointer to a reference of type 'int &'}}
    return 0;
}
constexpr int y = g<int &>(); // expected-error {{constexpr variable 'y' must be initialized by a constant expression}} expected-note {{in instantiation of function template specialization}}
constexpr int z = g<int>();
static_assert(z == 0, "");
