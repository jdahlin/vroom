// [class.mem]/15: a non-static data member of incomplete type is an error;
// clang drops the member and the class stays complete, so a derived class
// and a variable of it are analysed.
struct A;
struct B {
  A a;
  int x;
};
struct C : B {};
C c;
