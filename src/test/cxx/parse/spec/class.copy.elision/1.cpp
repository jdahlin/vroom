// [class.copy.elision] Example 1 (paragraph 2), docs/cxx-spec/class/class.copy.elision.md
// needs: P5
// status: annotated

class Thing {
public:
  Thing();
  ~Thing();
  Thing(const Thing&);
};

Thing f() {
  Thing t;
  return t;
}

Thing t2 = f();

struct A {
  void *p;
  constexpr A(): p(this) {}
};

constexpr A g() {
  A loc;
  return loc;
}

constexpr A a;          // well-formed, a.p points to a
constexpr A b = g();    // error: b.p would be dangling ([expr.const]) // semantic

void h() {
  A c = g();            // well-formed, c.p may point to c or to an ephemeral temporary
}
