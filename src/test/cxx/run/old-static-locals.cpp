// static locals with constant initializers: scalars, arrays (of unknown bound too), constexpr, a POD class
// (in an inline function: per TU in vcc, one object across TUs in C++ -- not checked here)
struct P { int a, b; };
static int counter() { static int n = 10; return ++n; }
static const char *name() { static const char s[] = "abc"; return s; }
static int table(int i) { static constexpr int t[4] = { 2, 3, 5, 7 }; return t[i]; }
static int pod() { static P p; p.a++; return p.a; }
inline int shared() { static int k; return ++k; }
int main() { counter(); int c = counter(); int p = pod(); p = pod(); shared(); return c == 12 && name()[2] == 'c' && table(3) == 7 && p == 2 && shared() == 2 ? 0 : 1; }
