// RUN: %clang_cc1 -std=c++11 -fsyntax-only %s
// [except.spec]/1, [class.mem]/6 (C++14 wording): a function declarator's
// dynamic-exception-specification, its type-ids analysed; in a member
// function's it is a complete-class context.
struct A {
  struct B {
    void f() throw(A);
    void g() throw(B);
  };
  void f() throw(A);
  void h() const throw(int, char *);
};
struct C {};
void a() throw(C);
void b() throw(int, C *, const C &);
void c() throw(void *);
enum E { e };
void d() throw(E);
template <class T> void t() throw(T);
