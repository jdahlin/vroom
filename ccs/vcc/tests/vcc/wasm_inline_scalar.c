/* Scalar calls, conversions, argument effects and conservative fallbacks. */
static volatile unsigned effects;
static int external_value;
static int add(int x, int y) { return x + y * 2; }
static signed char signed_narrow(signed char x) { return x; }
static _Bool bool_id(_Bool x) { return x; }
static unsigned char narrow(unsigned char x) { return x + 3; }
static int sum8(int a,int b,int c,int d,int e,int f,int g,int h) { return a+b+c+d+e+f+g+h; }
static int sum9(int a,int b,int c,int d,int e,int f,int g,int h,int i) { return a+b+c+d+e+f+g+h+i; }
static int scan_heavy(int x) { return x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+external_value; }
static unsigned mix(unsigned a, unsigned b) { return ((a ^ b) + (a << 3)) * 7u; }
static int first(int x, int unused) { return x; }
static const char *advance(const char *p, int n) { return p + n; }
static double half(double x) { return x * 0.5; }
static int choose(int x, int y) { return x ? x : y; }
static int side(int x) { return ++effects + x; }
static int add1(int x) { return x + 1; }
#define CAST_FN(x) ((int (*)(int))(x))
#define CAST_10(x) CAST_FN(CAST_FN(CAST_FN(CAST_FN(CAST_FN(CAST_FN(CAST_FN(CAST_FN(CAST_FN(CAST_FN(x))))))))))
static int deep_cast_wrapper(int x) { return CAST_10(CAST_10(CAST_10(CAST_10(CAST_10(CAST_10(add1))))))(x); }
#undef CAST_10
#undef CAST_FN
static int wrapper(int x) { return side(x) + 1; }
static int wrapper_twice(int x) { return wrapper(x) + wrapper(x); }
static int mutual_b(int x);
static int mutual_a(int x) { return x ? mutual_b(x - 1) + 1 : 0; }
static int mutual_b(int x) { return x ? mutual_a(x - 1) + 1 : 0; }
static int recurse(int x) { return x ? recurse(x - 1) + 1 : 0; }
static __attribute__((noinline)) int noinline_add(int x) { return x + 1; }
int wi_sum8(int x) { return sum8(x,1,2,3,4,5,6,7); }
int wi_sum9(int x) { return sum9(x,1,2,3,4,5,6,7,8); }
int wi_scan_budget(int x) { int z = 0;
 z += scan_heavy(x);
 z += scan_heavy(x);
 z += scan_heavy(x);
 z += scan_heavy(x);
 z += scan_heavy(x);
 z += scan_heavy(x);
 z += scan_heavy(x);
 z += scan_heavy(x);
 return z + add(x, 1); }
int wi_budget(int x) { int z = 0;
 z += add(x, 0);
 z += add(x, 1);
 z += add(x, 2);
 z += add(x, 3);
 z += add(x, 4);
 z += add(x, 5);
 z += add(x, 6);
 z += add(x, 7);
 z += add(x, 8);
 z += add(x, 9);
 z += add(x, 10);
 z += add(x, 11);
 z += add(x, 12);
 z += add(x, 13);
 z += add(x, 14);
 z += add(x, 15);
 z += add(x, 16);
 z += add(x, 17);
 z += add(x, 18);
 z += add(x, 19);
 z += add(x, 20);
 z += add(x, 21);
 z += add(x, 22);
 z += add(x, 23);
 z += add(x, 24);
 z += add(x, 25);
 z += add(x, 26);
 z += add(x, 27);
 z += add(x, 28);
 z += add(x, 29);
 z += add(x, 30);
 z += add(x, 31);
 z += add(x, 32);
 z += add(x, 33);
 z += add(x, 34);
 z += add(x, 35);
 z += add(x, 36);
 z += add(x, 37);
 z += add(x, 38);
 z += add(x, 39);
 return z; }
unsigned wi_mix(unsigned a, unsigned b) { return mix(a, b); }
int wi_add(int x) { return add(x, 3); }
int wi_nested(int x) { return add(add(x, 1), 2); }
int wi_effect(int x) { return add(x, ++effects); }
int wi_unused(int x) { return first(x, ++effects); }
const char *wi_pointer(const char *p) { return advance(p, 2); }
int wi_signed_narrow(int x) { return signed_narrow(x); }
int wi_bool(int x) { return bool_id(x); }
int wi_narrow(int x) { return narrow(x); }
double wi_float(double x) { return half(x); }
int wi_choose(int x, int y) { return choose(x, y); }
int wi_side(int x) { return side(x); }
int wi_deep_cast(int x) { return deep_cast_wrapper(x); }
int wi_wrapper(int x) { return wrapper(x); }
int wi_wrapper_twice(int x) { return wrapper_twice(x); }
int wi_mutual(int x) { return mutual_a(x); }
int wi_recurse(int x) { return recurse(x); }
int wi_noinline(int x) { return noinline_add(x); }
int main(void)
{
 for (unsigned i = 0; i < 256; ++i)
  if (wi_mix(i, i * 3) != (((i ^ (i * 3)) + (i << 3)) * 7u)) return 6;
 if (wi_scan_budget(1) != 163 || wi_budget(1) != 1600 || wi_sum8(1) != 29 || wi_sum9(1) != 37 || wi_add(7) != 13 || wi_nested(5) != 11 || wi_narrow(254) != 1 || wi_signed_narrow(255) != -1 || wi_bool(9) != 1 || wi_bool(0) != 0) return 1;
 if (wi_float(8.0) != 4.0 || wi_choose(0, 9) != 9 || wi_choose(4, 9) != 4) return 2;
 if (wi_effect(5) != 7 || effects != 1 || wi_unused(5) != 5 || effects != 2 || wi_side(5) != 8 || effects != 3) return 3;
 const char *p = "abcd";
 if (wi_pointer(p) != p + 2) return 5;
 if (wi_deep_cast(5) != 6 || wi_wrapper(5) != 10 || effects != 4 || wi_wrapper_twice(5) != 23 || effects != 6) return 8;
 if (wi_mutual(8) != 8 || wi_recurse(4) != 4 || wi_noinline(4) != 5) return 4;
 return 0;
}
