/* vcc-xfail-wasm: TODO.md _BitInt remaining targets: wasm runtime precision not implemented. */
/* Precise-width runtime arithmetic, load/store, conversion and comparisons. */
typedef unsigned __int128 U;
typedef __int128 S;
static volatile unsigned long seed = 0xfedcba9876543210UL;
#define TEST(N) \
static int test##N(void) { \
 typedef unsigned _BitInt(N) B; \
 U mask = (U)-1 >> (128-N); \
 U a = (((U)seed << 64) | seed) & mask, b = ((U)seed >> 7) & mask; \
 B x = (B)a, y = (B)b; \
 if ((U)(x+y) != ((a+b)&mask)) return 1; \
 if ((U)(x-y) != ((a-b)&mask)) return 2; \
 if ((U)(x*y) != ((a*b)&mask)) return 3; \
 if ((U)(~x) != ((~a)&mask)) return 4; \
 if ((U)(x^y) != (a^b)) return 5; \
 if ((x<y) != (a<b) || (x==y) != (a==b)) return 6; \
 for (int k=0; k<N; k++) { \
  if ((U)(x<<k) != ((a<<k)&mask)) return 7; \
  if ((U)(x>>k) != (a>>k)) return 8; \
 } \
 if (b && ((U)(x/y) != a/b || (U)(x%y) != a%b)) return 9; \
 if ((U)x++ != a || (U)x != ((a+1)&mask)) return 10; \
 x = (B)mask; \
 if ((U)++x != 0 || (U)x != 0) return 11; \
 x = (B)a; x += y; \
 if ((U)x != ((a+b)&mask)) return 12; \
 x *= y; \
 if ((U)x != ((((a+b)&mask)*b)&mask)) return 13; \
 return 0; \
}
#define SIGNED(N) \
static int signed##N(void) { \
 typedef _BitInt(N) B; \
 U a = ((U)seed << 64) | seed; \
 S s = (S)(a << (128-N)) >> (128-N); \
 B x = (B)a; \
 if ((S)x != s || (x < 0) != (s < 0)) return 1; \
 for (int k=0; k<N; k++) \
  if ((S)(x >> k) != (s >> k)) return 2; \
 if ((S)(~x) != ~s) return 3; \
 B y = (B)3; \
 if ((S)(x/y) != s/3 || (S)(x%y) != s%3) return 4; \
 return 0; \
}
TEST(1) TEST(2) TEST(7) TEST(8) TEST(9) TEST(16) TEST(17) TEST(31)
TEST(32) TEST(33) TEST(63) TEST(64) TEST(65) TEST(96) TEST(127) TEST(128)
SIGNED(7) SIGNED(9) SIGNED(17) SIGNED(31) SIGNED(33) SIGNED(63)
SIGNED(65) SIGNED(96) SIGNED(127) SIGNED(128)
static int overflow(void)
{
 volatile int a = 63, b = 1;
 _BitInt(7) s;
 unsigned _BitInt(7) u;
 if (!__builtin_add_overflow(a,b,&s) || s != -64) return 1;
 if (__builtin_add_overflow(a,b,&u) || u != 64) return 2;
 if (!__builtin_mul_overflow(a,a,&s) || s != 1) return 3;
 if (!__builtin_mul_overflow(a,a,&u) || u != 1) return 4;
 if (!__builtin_sub_overflow(0,b,&u) || u != 127) return 5;
 if (__builtin_sub_overflow(0,b,&s) || s != -1) return 6;
 unsigned _BitInt(1) one;
 if (!__builtin_add_overflow(b,b,&one) || one != 0) return 7;
 return 0;
}
int main(void) {
 if (overflow()) return 200;
 static const unsigned long seeds[] = { 0, 1, ~0UL, 0x8000000000000000UL,
  0x7fffffffffffffffUL, 0xaaaaaaaaaaaaaaaaUL, 0x0101010101010101UL,
  0xfedcba9876543210UL };
 for (unsigned i = 0; i < sizeof(seeds)/sizeof(seeds[0]); i++) {
 seed = seeds[i];
#define RUN(N) if (test##N()) return N;
 RUN(1) RUN(2) RUN(7) RUN(8) RUN(9) RUN(16) RUN(17) RUN(31)
 RUN(32) RUN(33) RUN(63) RUN(64) RUN(65) RUN(96) RUN(127) RUN(128)
#define SRUN(N) if (signed##N()) return 64+N;
 SRUN(7) SRUN(9) SRUN(17) SRUN(31) SRUN(33) SRUN(63)
 SRUN(65) SRUN(96) SRUN(127) SRUN(128)
 }
 return 0;
}
