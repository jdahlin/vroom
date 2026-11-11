static volatile int observed;
static unsigned calls, other_calls;
int ri_sub(int x) { return x - x; }
unsigned long long ri_xor(unsigned long long x) { return x ^ x; }
int ri_and(int x) { return x & x; }
unsigned ri_or(unsigned x) { return x | x; }
int ri_branch(int x) { if (x - x) return 1; return 2; }
int ri_volatile(void) { return observed - observed; }
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
     ri_distinct(4, 7) != -3 || ri_address(9)) return 1;
 observed = 9;
 if (ri_volatile() || ri_volatile_ptr(&observed)) return 2;
 calls = other_calls = 0;
 if (ri_calls() != (1U ^ 2U) || calls != 1 || other_calls != 1) return 3;
 return 0;
}
