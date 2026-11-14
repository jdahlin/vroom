/* dense switches (jump tables at -O1 and up): holes, negative and 64-bit selectors,
 * fallthrough, no default, a loop-carried dispatch, inline asm in the same function */
static int ref_a(int x)
{
 if (x == 0) return 10; if (x == 1) return 21; if (x == 2) return 33;
 if (x == 4) return 7; if (x == 5) return 9; if (x == 7) return 1;
 return -1;
}
__attribute__((noinline)) static int sw_a(int x)
{
 switch (x) {
 case 0: return 10; case 1: return 21; case 2: return 33;
 case 4: return 7; case 5: return 9; case 7: return 1;
 default: return -1;
 }
}
__attribute__((noinline)) static int sw_neg(int x)
{
 switch (x) { case -2: return 1; case -1: return 2; case 0: return 3; case 1: return 4; case 3: return 5; }
 return 0;
}
__attribute__((noinline)) static int sw_u64(unsigned long long x)
{
 switch (x) {
 case 0x100000000ULL: return 1; case 0x100000001ULL: return 2; case 0x100000002ULL: return 3;
 case 0x100000003ULL: return 4; case 0x100000005ULL: return 5;
 }
 return 0;
}
__attribute__((noinline)) static int sw_char(signed char x)
{
 switch (x) { case 'a': return 1; case 'b': return 2; case 'c': return 3; case 'e': return 4; case 'f': return 5; default: return 9; }
}
__attribute__((noinline)) static int sw_fall(unsigned x)
{
 int n = 0;
 switch (x) { case 3: n += 1; case 4: n += 2; case 5: n += 4; break; case 6: n += 8; case 8: n += 16; }
 return n;
}
__attribute__((noinline)) static int sw_asm(int x)
{
 int r;
 switch (x) { case 1: r = 5; break; case 2: r = 6; break; case 3: r = 7; break; case 4: r = 8; break; default: r = 0; }
 __asm__ volatile("" : "+r"(r));
 return r;
}
int main(void)
{
 for (int i = -9; i < 20; i++)
  if (sw_a(i) != ref_a(i)) return 1;
 for (int i = -9; i < 9; i++) {
  int e = i == -2 ? 1 : i == -1 ? 2 : i == 0 ? 3 : i == 1 ? 4 : i == 3 ? 5 : 0;
  if (sw_neg(i) != e) return 2;
 }
 if (sw_neg(-2147483647 - 1) || sw_neg(2147483647)) return 3;
 for (unsigned long long i = 0xffffffffffULL; i < 0x100000009ULL; i++) {
  int e = i == 0x100000000ULL ? 1 : i == 0x100000001ULL ? 2 : i == 0x100000002ULL ? 3 : i == 0x100000003ULL ? 4 : i == 0x100000005ULL ? 5 : 0;
  if (sw_u64(i) != e) return 4;
 }
 if (sw_u64(1) || sw_u64(~0ULL) || sw_u64(5)) return 5;
 if (sw_char('a') != 1 || sw_char('d') != 9 || sw_char('f') != 5 || sw_char(-1) != 9 || sw_char(-128) != 9) return 6;
 if (sw_fall(3) != 7 || sw_fall(4) != 6 || sw_fall(5) != 4 || sw_fall(6) != 24 || sw_fall(7) != 0 ||
     sw_fall(8) != 16 || sw_fall(2) != 0 || sw_fall(0xffffffffu) != 0) return 7;
 int s = 0;
 for (int i = 0; i < 50; i++) s += sw_a(i % 9);
 int t = 0;
 for (int i = 0; i < 50; i++) t += ref_a(i % 9);
 if (s != t) return 8;
 if (sw_asm(1) != 5 || sw_asm(4) != 8 || sw_asm(5) || sw_asm(0) || sw_asm(-1)) return 9;
 return 0;
}
