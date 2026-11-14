// overloaded operators (members, at namespace scope, templates) and conversion functions
struct V { int v; V(int x) : v(x) {} 
  V operator+(const V &o) const { return V(v + o.v); }
  bool operator==(const V &o) const { return v == o.v; }
  V &operator+=(int d) { v += d; return *this; }
  int operator[](int i) const { return v + i; }
  int operator()(int a, int b) const { return v + a + b; }
  explicit operator bool() const { return v != 0; }
  V operator-() const { return V(-v); }
  V &operator++() { ++v; return *this; }
};
static V operator*(const V &a, const V &b) { return V(a.v * b.v); }
template <class T> struct L { T x; template <class R> bool operator==(const R &r) const { return x == r; } };
struct D { template <class T> L<T> operator<<(const T &t) { L<T> l; l.x = t; return l; } };
static int f1() { V a(1), b(2); V c = a + b; return c.v; }
static int f2() { V a(1), b(2); return a == b; }
static int f3() { V a(1); a += 4; return a.v; }
static int f4() { V a(1); return a[3]; }
static int f5() { V a(1); return a(2, 3); }
static int f6() { V a(1); if (a) return 1; return 0; }
static int f7() { V a(3), b(2); V c = a * b; return c.v; }
static int f8() { V a(3); V c = -a; ++c; return c.v; }
static int f9() { return (D() << 5) == 5; }
static int f10() { V a(1); return !a; }
int main() { return f1() + f2() + f3() + f4() + f5() + f6() + f7() + f8() + f9() + f10() == 24 ? 0 : 1; }
