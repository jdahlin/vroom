/* Ordered floating comparisons, NaNs, mixed types and once-only evaluation. */
extern void abort(void);
#define CHECK(c) do { if (!(c)) abort(); } while (0)
#define TEST(T, suffix) \
static void test_##suffix(void) { \
 volatile T values[] = { -1.0L, -0.0L, 0.0L, 1.0L, __builtin_inf(), __builtin_nan("") }; \
 for (int i=0;i<6;i++) for(int j=0;j<6;j++) { \
  T a=values[i], b=values[j]; \
  int unordered = i==5 || j==5; \
  CHECK(__builtin_isunordered(a,b)==unordered); \
  CHECK(__builtin_isgreater(a,b)==(!unordered && a>b)); \
  CHECK(__builtin_isgreaterequal(a,b)==(!unordered && a>=b)); \
  CHECK(__builtin_isless(a,b)==(!unordered && a<b)); \
  CHECK(__builtin_islessequal(a,b)==(!unordered && a<=b)); \
  CHECK(__builtin_islessgreater(a,b)==(!unordered && a!=b)); \
 } \
}
TEST(float, f)
TEST(double, d)
static int calls;
static float f(void) { calls++; return 2.0f; }
static double l(void) { calls++; return 3.0; }
int main(void) {
 test_f(); test_d();
 CHECK(__builtin_isless(f(),l())); CHECK(calls==2);
 CHECK(__builtin_islessgreater(l(),f())); CHECK(calls==4);
 CHECK(!__builtin_isunordered(f(),l())); CHECK(calls==6);
 volatile float a=2;
 volatile double b=2.0000000001;
 CHECK(__builtin_isless(a,b));
 volatile double c=2.000000000000001;
 CHECK(__builtin_isless(a,c));
 return 0;
}
