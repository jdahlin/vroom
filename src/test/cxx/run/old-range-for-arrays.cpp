// range-based for over arrays and braced lists: an index loop over the array (the list's elements in a
// local array of their type), the variable by copy, reference or pointer; break and continue
namespace std {	// (no library here: what a braced range needs, as libc++ has it)
template <class E> class initializer_list {
  const E *b; unsigned long n;
  constexpr initializer_list(const E *x, unsigned long k) : b(x), n(k) {}
public:
  constexpr initializer_list() : b(nullptr), n(0) {}
  constexpr const E *begin() const { return b; }
  constexpr const E *end() const { return b + n; }
};
}
int main() {
  int a[5] = {1, 2, 3, 4, 5}, sum = 0, n = 0;
  for (int x : a) sum += x;			// 15
  for (int &x : a) x *= 2;			// a = 2 4 6 8 10
  for (const auto &x : a) { if (x == 4) continue; if (x == 8) break; sum += x; }	// + 2 + 6
  for (auto x : {1, 2, 3}) sum += x * 100;	// + 600
  for (const auto *s : {"ab", "cde", "f"}) n += s[0];	// 97 + 99 + 102
  double d[2] = {0.5, 1.5};
  for (double &v : d) v += 1;
  return sum == 15 + 8 + 600 && n == 298 && d[0] == 1.5 && d[1] == 2.5 && a[4] == 10 ? 0 : 1;
}
