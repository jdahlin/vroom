// std: c++20
// range-based for over classes: begin()/end() copied, the iterator's != (or !(a == b)), ++ and *; pointers as
// iterators; T x(e) of scalars; a trailing requires-clause naming the parameters
struct It { int *p; bool operator!=(const It &o) const { return p != o.p; } It &operator++() { ++p; return *this; } int &operator*() const { return *p; } };
struct It2 { int *p; bool operator==(const It2 &o) const { return p == o.p; } It2 &operator++() { ++p; return *this; } int &operator*() const { return *p; } };
struct V { int a[4]; It begin() { return {a}; } It end() { return {a + 4}; } };
struct W { int a[3]; It2 begin() { return {a}; } It2 end() { return {a + 3}; } };
struct P { int a[2]; int *begin() { return a; } int *end() { return a + 2; } };
int main() {
  V v; W w; P p; int s = 0;
  for (int i = 0; i < 4; i++) v.a[i] = i + 1; w.a[0] = 5; w.a[1] = 6; w.a[2] = 7; p.a[0] = 8; p.a[1] = 9;
  for (int &x : v) x *= 10;
  for (int x : v) s += x;		// 100
  for (auto x : w) s += x;		// 18 (C++20: != from ==)
  for (const int &x : p) s += x;	// 17
  return s == 135 ? 0 : 1;
}
