// temporaries with destructors in operands that may not run (a && f(T(x)), c ? n : f(T(x)), c && g().v):
// built only when the operand runs, destroyed only then (through a pointer set once built); constructor
// counts and values as clang's
static int live = 0, made = 0;
struct S { int v; S(int x = 0) : v(x) { ++live; ++made; } S(const S &o) : v(o.v) { ++live; ++made; } ~S() { --live; } };
int val(const S &s) { return s.v; }
S mk(int v) { return S(v); }
int main() {
  int r = 0;
  for (int i = 0; i < 4; i++) {
    bool c = i & 1;
    r += (c && val(S(i)) == i) ? 1 : 0;
    r += c ? 10 : val(S(5));
    r += c && mk(3).v == 3 ? 100 : 0;
  }
  return r == 2 + 20 + 10 + 200 && made == 6 && live == 0 ? 0 : 1;
}
