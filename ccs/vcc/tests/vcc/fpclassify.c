/* Runtime class codes, clang evaluation order, and one operand evaluation. */
#define CHECK(T, suffix, minimum, tiny) \
static int check_##suffix(void) { \
 volatile T values[] = {0.0, -0.0, 1.0, -1.0, minimum, tiny, \
 __builtin_huge_val##suffix(), -__builtin_huge_val##suffix(), __builtin_nan##suffix("")}; \
 int expected[] = {55,55,33,33,33,44,22,22,11}; \
 for (int i=0; i<9; i++) { \
  int a=10,b=21,c=32,d=43,e=54,j=i; \
  int got=__builtin_fpclassify(++a,++b,++c,++d,++e,values[j++]); \
  if (got!=expected[i] || j!=i+1 || a!=10+(i>=2) || b!=21+(i>=2 && i!=8) || c!=32+(i>=2 && i<=5) || d!=43+(i>=2 && i<=5) || e!=55) return 1; \
 } return 0; \
}
CHECK(float, f, __FLT_MIN__, __FLT_DENORM_MIN__)
CHECK(double, , __DBL_MIN__, __DBL_DENORM_MIN__)
CHECK(long double, l, __LDBL_MIN__, __LDBL_DENORM_MIN__)
static int constant_class = __builtin_fpclassify(11,22,33,44,55,1.0);
int main(void) { return constant_class != 33 || check_f() || check_() || check_l(); }
