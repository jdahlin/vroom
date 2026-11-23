// an unsuffixed integer literal too big for int: long (decimal) or the first of unsigned int, long,
// unsigned long (hex, octal, binary) -- it was int, truncated (json's make_double(0, 0, 0x000FFFFFFFFFFFFF))
static int k(int) { return 1; }
static int k(unsigned) { return 2; }
static int k(long) { return 3; }
static int k(unsigned long) { return 4; }
static int k(long long) { return 5; }
static int k(unsigned long long) { return 6; }
int main() {
  unsigned long a = 4294967296, b = 0x000FFFFFFFFFFFFF;
  int r = 0;
  r |= k(2147483647) != 1;
  r |= k(2147483648) != 3;
  r |= k(0x80000000) != 2;
  r |= k(0x100000000) != 3;
  r |= k(0x8000000000000000) != 4;
  r |= k(4294967295u) != 2;
  r |= k(4294967296u) != 4;
  r |= k(1l) != 3;
  r |= k(0xFFFFFFFFFFFFFFFFl) != 4;
  r |= k(1ll) != 5;
  r |= k(1ull) != 6;
  r |= k(017777777777) != 1;
  r |= k(020000000000) != 2;
  r |= k(0b11111111111111111111111111111111) != 2;
  r |= sizeof(0x100000000) != 8;
  return r || a != 4294967296ul || b != 4503599627370495ul;
}
