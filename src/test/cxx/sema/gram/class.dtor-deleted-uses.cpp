// [class.dtor]/6, [class.base.init]/12, [class.default.ctor]/2: a deleted
// destructor is used by a variable of the class, by a defaulted default
// constructor of a class with such a subobject, and by a user-provided
// constructor that would destroy such a subobject; bench/cxx-sema.py
// compares each verdict with clang's.
struct A { ~A() = delete; };
A g1;
extern A g2;
struct Holder { A a; };
Holder h1;
struct Derived : A {};
Derived d1;
struct Plain { int x; };
Plain ok1;
struct C { C(); A a; };
C::C() {}
struct E { A *p; E() {} };
E ok2;
struct Wrapped { Wrapped(); Plain p; };
Wrapped::Wrapped() {}
void f() {
  A a1;
  static A a2;
  Plain ok3;
}
