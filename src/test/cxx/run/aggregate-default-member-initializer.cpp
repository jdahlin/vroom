extern "C" int printf(const char *, ...);
struct V { unsigned long a = 0; unsigned long b = 7, c = 1 + 2; int *p = nullptr; bool f = true; double d = -2.5; };
inline constexpr V none = {};
V one = {5};
V two{1, 2};
int main() { V l = {}; V m = {9, 8};
  printf("%lu %lu %lu %d %d %f\n", none.b, one.b, two.c, l.f, m.p == nullptr, m.d + two.a); return none.a + (long)one.a - 5; }
