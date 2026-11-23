// llvm-test-suite SingleSource/Regression/C++/2003-06-08-BaseType.cpp
// (Apache-2.0 WITH LLVM-exception): a base class with a constructor


struct foo {
  int y;
  foo();
};

struct bar : public foo {
  //int x;
  bar();
};

bar::bar() { }
foo::foo() { }

int main() { return 0; }
