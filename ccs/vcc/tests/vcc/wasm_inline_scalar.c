/* Scalar calls, conversions, argument effects and conservative fallbacks. */
static volatile unsigned effects;
static int add(int x, int y) { return x + y * 2; }
static unsigned char narrow(unsigned char x) { return x + 3; }
static int sum8(int a,int b,int c,int d,int e,int f,int g,int h) { return a+b+c+d+e+f+g+h; }
static int sum9(int a,int b,int c,int d,int e,int f,int g,int h,int i) { return a+b+c+d+e+f+g+h+i; }
static int first(int x, int unused) { return x; }
static const char *advance(const char *p, int n) { return p + n; }
static double half(double x) { return x * 0.5; }
static int choose(int x, int y) { return x ? x : y; }
static int side(int x) { return ++effects + x; }
static int recurse(int x) { return x ? recurse(x - 1) + 1 : 0; }
static __attribute__((noinline)) int noinline_add(int x) { return x + 1; }
int wi_sum8(int x) { return sum8(x,1,2,3,4,5,6,7); }
int wi_sum9(int x) { return sum9(x,1,2,3,4,5,6,7,8); }
int wi_add(int x) { return add(x, 3); }
int wi_nested(int x) { return add(add(x, 1), 2); }
int wi_effect(int x) { return add(x, ++effects); }
int wi_unused(int x) { return first(x, ++effects); }
const char *wi_pointer(const char *p) { return advance(p, 2); }
int wi_narrow(int x) { return narrow(x); }
double wi_float(double x) { return half(x); }
int wi_choose(int x, int y) { return choose(x, y); }
int wi_side(int x) { return side(x); }
int wi_recurse(int x) { return recurse(x); }
int wi_noinline(int x) { return noinline_add(x); }
int main(void)
{
 if (wi_sum8(1) != 29 || wi_sum9(1) != 37 || wi_add(7) != 13 || wi_nested(5) != 11 || wi_narrow(254) != 1) return 1;
 if (wi_float(8.0) != 4.0 || wi_choose(0, 9) != 9 || wi_choose(4, 9) != 4) return 2;
 if (wi_effect(5) != 7 || effects != 1 || wi_unused(5) != 5 || effects != 2 || wi_side(5) != 8 || effects != 3) return 3;
 const char *p = "abcd";
 if (wi_pointer(p) != p + 2) return 5;
 if (wi_recurse(4) != 4 || wi_noinline(4) != 5) return 4;
 return 0;
}
