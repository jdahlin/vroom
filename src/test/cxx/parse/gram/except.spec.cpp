// [except.spec]/3, [diff.cpp17.except]/1: throw() is still accepted after
// a function declarator in C++20, as noexcept(true) (clang's extension, no
// diagnostic), also in an extern "C" declaration (libc++'s exception_ptr.h).
extern "C" void f1(int) throw();
void f2() throw();
struct S {
  void g() throw();
  S() throw();
};
void f2() throw() {}
