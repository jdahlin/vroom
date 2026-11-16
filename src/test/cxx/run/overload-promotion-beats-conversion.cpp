extern "C" int printf(const char*, ...);
/* os << e of an unscoped enumeration: operator<<(int) by promotion, not the conversions to char16_t, long ... */
struct O {
  int operator<<(int) { return 1; }
  int operator<<(long) { return 2; }
  int operator<<(unsigned) { return 3; }
  int operator<<(double) { return 4; }
  int operator<<(char16_t) = delete;
  int operator<<(char32_t) = delete;
};
enum E { a, b };
enum F : unsigned char { f0 };
enum class S : int { s };
int g(int) { return 1; } int g(double) { return 2; } int g(char16_t) { return 3; }
int h(double) { return 1; } int h(int) { return 2; } int h(long double) { return 3; }
int main() { O o; short sh = 1; bool bo = true; float fl = 1;
  printf("%d %d %d %d %d %d %d\n", o << b, o << f0, g(sh), g(bo), g('x'), h(fl), o << sh); }
