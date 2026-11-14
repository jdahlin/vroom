// the implicit copy constructor of a class with constructors, member by member: a base with a
// user-provided copy, a const member, an array, a class member; thrown by copy (throw ex with const E& ex)
struct B { int n; B(int x) : n(x) {} B(const B &o) : n(o.n + 100) {} };
struct M { int v; M(int x) : v(x) {} M(const M &o) : v(o.v + 10) {} };
struct E : B { const int id; int a[3]; M m; E(int i) : B(i), id(i), m(i) { a[0] = 1, a[1] = 2, a[2] = 3; } };
template <class T> int thr(const T &ex) { throw ex; }
int main() {
  E e(5);
  E f(e);
  int r = 0;
  try { thr(e); } catch (const E &x) { r = x.id + x.n + x.m.v; }
  return f.n == 105 && f.id == 5 && f.a[2] == 3 && f.m.v == 15 && r == 5 + 105 + 15 ? 0 : 1;
}
