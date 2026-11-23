// [expr.mptr.oper] to [expr.log.or], [expr.cond], [expr.ass], [expr.comma]:
// every binary operator between its neighbours in precedence, so that a
// wrong grouping changes the value (bench/cxx-parse.py, claims).
constexpr int a = 7, b = 3, c = 2;
struct W {
  int v;
  constexpr int operator<=>(const W &o) const { return v - o.v; }
  constexpr W operator<<(int n) const { return W{v << n}; }
};
constexpr W w7{7}, w3{3};

static_assert(a - b - c == 2);
static_assert(a - b * c == 1);
static_assert(a * b % 4 / c == 0 + 0);
static_assert(a + b << c == 40);
static_assert(a << c + 1 == 56);
static_assert(a >> 1 << 2 == 12);
static_assert((w7 <=> w3 << 1) == 1);
static_assert(1 < w7 <=> w3);
static_assert((w7 <=> w3 > 3) == 1);
static_assert(a < b == b > a);
static_assert(a <= b != a >= b);
static_assert((a & b == b) == 1);
static_assert((a ^ b & c) == 5);
static_assert((a | b ^ b) == 7);
static_assert((c | 4 && 0) == 0);
static_assert((1 || 0 && 0) == 1);
static_assert((0 ? 1 : 0 ? 2 : 3) == 3);
static_assert((1 ? 0 : 1 || 1) == 0);
static_assert((a, b, c) == 2);

struct S {
  int m;
  constexpr int f() const { return m; }
};
constexpr S s{5};
constexpr const S *p = &s;
constexpr int S::*pm = &S::m;
static_assert(s.*pm * 2 == 10);
static_assert(p->*pm + s.*pm == 10);
static_assert(-(s.*pm) == -5);
static_assert(*p.*pm == 5);
static_assert((&s)->*pm == (*p).*pm);

constexpr int assigned(int x) {
  int y = 0, z = 0;
  y = z = x + 1;
  y += z *= 2;
  y <<= 1, z >>= 1;
  y |= z &= 3;
  y ^= 1;
  y %= 100, y /= 1, y -= 0;
  return x ? y : z = 9;
}
int assignments = (assigned(1), 0);
