// [class.base.init] Example 6 (paragraph 9), docs/cxx-spec/class/class.base.init.md
// needs: P5
// status: annotated

struct A {
  A();
};

struct B {
  B(int);
};

struct C {
  C() { }               // initializes members as follows:
  A a;                  // OK: calls A::A()
  const B b;            // error: B has no default constructor // semantic
  int i;                // OK: i has indeterminate value
  int j = 5;            // OK: j has the value 5
};
