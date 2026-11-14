// value arguments of a parameter declared with another parameter's type (template <class T, T v>): L <that type> <value> E
template <class T, T v> struct ic { int get() const { return 1; } };
template <class C, class I, I e> struct tb { static bool lt(C a, C b) { return a < b; } };
int main() { ic<bool, false> f; ic<char, 'a'> a; ic<unsigned short, 65535> u; tb<char16_t, unsigned short, 65535> t; char16_t x = 1, y = 2; return f.get() + a.get() + u.get() - 3 + t.lt(x, y) - 1; }
