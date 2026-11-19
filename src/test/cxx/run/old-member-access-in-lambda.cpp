// x.m where m is a data member (here a base's): the member of x, never this->m, also in a lambda's body
// (its operator() has a this of its own) and in a member function (doctest's INFO: mb.m_stream = s)
struct MD { int *m_stream = nullptr; int t = 3; };
struct MB : MD { int x = t + 1; MB(int a) { t = a; } int same(const MB &o) const { return o.t == t && o.x == x; } };
int main() {
  int v = 7;
  auto f = [&](int *s) { MB mb(v); mb.m_stream = s; return mb.t + mb.x + (mb.m_stream == s); };
  MB a(1), b(2);
  int q;
  return f(&q) == 12 && !a.same(b) && a.same(a) ? 0 : 1;
}
