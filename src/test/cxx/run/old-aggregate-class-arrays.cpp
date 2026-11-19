// aggregates with an array of class objects: built from the array's own list (in a declaration and as a
// T{...} expression), destroyed element by element, copied element by element; return T{...} builds T in
// the caller's object (no copy: the counts are clang's); a defaulted move constructor built in place;
// c ? lvalue : prvalue of one class (a temporary built from either: a prvalue operand in place; no destructor: one in an
// operand that may not run is not built yet)
static int live = 0, made = 0;
struct S { int v; S(int x = 0) : v(x) { ++live; ++made; } S(const S &o) : v(o.v) { ++live; ++made; } ~S() { --live; } };
struct H { S e[2]; };
struct P { int k; S s; P(int a, int b) : k(a), s(b) {} P(P &&) = default; };
H make() { return H{{S(1), S(2)}}; }
int use(const H &h) { return h.e[1].v; }
template <class T> T &&mv(T &t) { return static_cast<T &&>(t); }
struct C { int v; C(int x) : v(x) {} C(const C &o) : v(o.v) {} };
C pick(bool c, C &h) { return c ? h : C(7); }
int main() {
  int r = 0;
  { H h = make(); r |= h.e[0].v != 1 || h.e[1].v != 2 || live != 2 || made != 2; }
  r |= live != 0;
  { H g = {{S(3)}}; r |= g.e[0].v != 3 || g.e[1].v != 0; H c = g; r |= c.e[0].v != 3; r |= use(H{{S(4), S(5)}}) != 5; }
  r |= live != 0;
  { P p(1, 2); P q(mv(p)); r |= q.k != 1 || q.s.v != 2; }
  r |= live != 0;
  { C h(1); r |= pick(true, h).v != 1 || pick(false, h).v != 7; }
  return r;
}
