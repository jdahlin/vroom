// RUN: %clang_cc1 -std=c++23 %s
// P0847R7 (C++23): parameter-declaration: attribute-specifier-seq_opt this_opt
// decl-specifier-seq declarator; [dcl.fct]/6 of n4861's successor.  The parse
// keeps `this` as a token of the parameter-declaration.
struct X {
  void f(this X self);
  void g(this const X &, int);
  void h(this auto &&s, int n = 0) {}
  static void k(int);
};
struct S {
  S(int, int);
};
void block() {
  S s(1, 2);
}
