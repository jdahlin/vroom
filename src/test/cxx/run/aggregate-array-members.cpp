// aggregates with array members at namespace scope and as static locals, built when the program starts:
// {{1, 2}} for an array member, an array of class elements each from its own list (std::array of structs,
// of std::arrays: json's kCachedPowers, utility_table); a static const one is not in read-only data
struct A2 { int v[2]; };
struct CP { unsigned long f; int e; int k; };
template <class T, unsigned long N> struct Arr { T e[N]; const T &operator[](unsigned long i) const { return e[i]; } };
A2 g1 = {{1, 2}};
static constexpr Arr<CP, 2> g2 = {{{1, 2, 3}, {4, 5, 6}}};
int loc() {
  static constexpr Arr<Arr<char, 2>, 3> t{{{{'0', '0'}}, {{'0', '1'}}, {{'0', '2'}}}};
  static const CP one{1, 2, 3};
  return t[2][1] + one.k;
}
int main() { return g1.v[1] == 2 && g2[1].k == 6 && loc() == '2' + 3 ? 0 : 1; }
