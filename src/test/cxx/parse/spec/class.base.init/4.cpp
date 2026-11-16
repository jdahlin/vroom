// [class.base.init] Example 4 (paragraph 7), docs/cxx-spec/class/class.base.init.md
// needs: P5
// status: well-formed (as far as the example says)

struct B1 { B1(int); /* ... */ };
struct B2 { B2(int); /* ... */ };
struct D : B1, B2 {
  D(int);
  B1 b;
  const int c;
};

D::D(int a) : B2(a+1), B1(a+2), c(a+3), b(a+4) { /* ... */ }
D d(10);
