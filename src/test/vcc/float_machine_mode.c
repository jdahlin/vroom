typedef float __attribute__((mode(DF))) df;
typedef double __attribute__((mode(SF))) sf;
typedef float post_df __attribute__((mode(__DF__)));
typedef double post_sf __attribute__((mode(__SF__)));
struct S { char a; float b __attribute__((mode(DF))); double c __attribute__((mode(SF))); };
_Static_assert(__builtin_types_compatible_p(df, double), "DF typedef");
_Static_assert(__builtin_types_compatible_p(sf, float), "SF typedef");
_Static_assert(__builtin_types_compatible_p(post_df, double), "DF postfix");
_Static_assert(__builtin_types_compatible_p(post_sf, float), "SF postfix");
_Static_assert(sizeof(((struct S *)0)->b) == 8, "DF member");
_Static_assert(sizeof(((struct S *)0)->c) == 4, "SF member");
static df add(df x, df y) { return x + y; }
int main(void) {
    float a __attribute__((mode(DF))) = 16777217.0;
    double b __attribute__((mode(SF))) = 16777217.0;
    struct S s = { 1, 223978.359375, 123128.75 };
    if (sizeof a != 8 || sizeof b != 4) return 1;
    if (a != 16777217.0 || b != 16777216.0) return 2;
    if (add(a, 1.0) != 16777218.0) return 3;
    if (s.b != 223978.359375 || s.c != 123128.75) return 4;
    return 0;
}
