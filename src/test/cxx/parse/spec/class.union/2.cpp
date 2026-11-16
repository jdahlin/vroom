// [class.union] Example 2 (paragraph 6), docs/cxx-spec/class/class.union.md
// needs: P5
// status: well-formed (as far as the example says)

union A { int x; int y[4]; };
struct B { A a; };
union C { B b; int k; };
int f() {
  C c;                  // does not start lifetime of any union member
  c.b.a.y[3] = 4;       // OK: S(c.b.a.y[3]) contains c.b and c.b.a.y;
                        // creates objects to hold union members c.b and c.b.a.y
  return c.b.a.y[3];    // OK: c.b.a.y refers to newly created object (see [basic.life])
}

struct X { const int a; int b; };
union Y { X x; int k; };
void g() {
  Y y = { { 1, 2 } };   // OK, y.x is active union member ([class.mem])
  int n = y.x.a;
  y.k = 4;              // OK: ends lifetime of y.x, y.k is active member of union
  y.x.b = n;            // undefined behavior: y.x.b modified outside its lifetime,
                        // S(y.x.b) is empty because X's default constructor is deleted,
                        // so union member y.x's lifetime does not implicitly start
}
