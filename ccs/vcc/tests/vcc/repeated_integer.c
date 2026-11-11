static volatile int observed;
static unsigned calls, other_calls;
int ri_sub(int x) { return x - x; }
unsigned long long ri_xor(unsigned long long x) { return x ^ x; }
int ri_and(int x) { return x & x; }
unsigned ri_or(unsigned x) { return x | x; }
int ri_eq(long long x) { return x == x; }
int ri_ne(unsigned x) { return x != x; }
int ri_lt(int x) { return x < x; }
int ri_ge(unsigned long long x) { return x >= x; }
int ri_cmp_branch(long long x) { if (x == x) return 5; return 8; }
int ri_float(double x) { return x == x; }
int ri_branch(int x) { if (x - x) return 1; return 2; }
int ri_volatile(void) { return observed - observed; }
unsigned ri_atomic_ptr(_Atomic unsigned *p) { return __c11_atomic_load(p, 5) ^ __c11_atomic_load(p, 5); }
int ri_volatile_ptr(volatile int *p) { return *p - *p; }
int ri_distinct(int x, int y) { return x - y; }
int ri_address(int x) { int *p = &x; return *p - x; }
static unsigned next(void) { return ++calls; }
static unsigned other(void) { other_calls++; return 2; }
unsigned ri_calls(void) { return next() ^ other(); }
int main(void)
{
 if (ri_sub(-2147483647-1) || ri_sub(3) || ri_xor(~0ULL) ||
     ri_and(-123) != -123 || ri_or(123) != 123 || ri_branch(42) != 2 ||
     ri_distinct(4, 7) != -3 || ri_address(9) || !ri_eq(-9) || ri_ne(9) ||
     ri_lt(-9) || !ri_ge(~0ULL) || ri_cmp_branch(3) != 5 || ri_float(__builtin_nan(""))) return 1;
 _Atomic unsigned atom = 7;
 if (ri_atomic_ptr(&atom)) return 5;
 observed = 9;
 if (ri_volatile() || ri_volatile_ptr(&observed)) return 2;
 calls = other_calls = 0;
 if (ri_calls() != (1U ^ 2U) || calls != 1 || other_calls != 1) return 3;
 return 0;
}
