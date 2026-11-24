// RUN: %clang_cc1 -std=c++17 -fsyntax-only -Wno-dynamic-exception-spec %s
// [except.spec]/4 against the implicit specifications of [except.spec]/8 and
// /12: a destructor and a deallocation function are no exemption from the
// rule; without a specification of its own a declaration has its implicit
// one (non-throwing), and two dynamic ones must name the same set.
struct A { ~A() throw(int); };
A::~A() throw(long) {}
struct B { ~B(); };
B::~B() throw(int) {}
struct C { void operator delete(void *) throw(int); };
void C::operator delete(void *) throw(long) {}
void operator delete(void *, int) throw(int);
void operator delete(void *, int) throw(long);
void operator delete(void *, long);
void operator delete(void *, long) throw();
void operator delete(void *, char);
void operator delete(void *, char) throw(int);
void f() throw(...);
void f() noexcept(false);
void h() throw(int, char);
void h() throw(char, int, char);
