// C++20: a == b and a != b with no operator that fits use the reversed candidate, b == a or !(b == a)
// (1 == j with only a member operator==(S) const, as json's)
// std: c++20
struct J { int v; bool operator==(const J &o) const { return v == o.v; }
  template <class S> bool operator==(S s) const { return v == (int)s; } };
struct K { int v; bool operator==(int x) const { return v == x; } };
int f1(J &j) { bool r = 1 == j; return r; }
int f2(K &k) { bool r = 3 == k; return r; }
int f3(K &k) { bool r = 3 != k; return r; }
int f4(J &j) { return 1 == j; }
int main() { J j{1}; K k{3}; return f1(j) && f2(k) && !f3(k) && f4(j) ? 0 : 1; }
