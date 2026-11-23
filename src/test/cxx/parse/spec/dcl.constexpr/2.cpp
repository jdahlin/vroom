// [dcl.constexpr] Example 2 (paragraph 3), docs/cxx-spec/dcl/dcl.constexpr.md
// needs: P5
// status: annotated

constexpr int square(int x)
  { return x * x; }             // OK
constexpr long long_max()
  { return 2147483647; }        // OK
constexpr int abs(int x) {
  if (x < 0)
    x = -x;
  return x;                     // OK
}
constexpr int first(int n) {
  static int value = n;         // error: variable has static storage duration // semantic
  return value;
}
constexpr int uninit() {
  struct { int a; } s;
  return s.a;                   // error: uninitialized read of s.a // semantic
}
constexpr int prev(int x)
  { return --x; }               // OK
constexpr int g(int x, int n) { // OK
  int r = 1;
  while (--n > 0) r *= x;
  return r;
}
