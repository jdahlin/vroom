/* __builtin_abs, labs and llabs of a constant are constant expressions
 * (clang): the C front end folds them through the shared fold table's
 * operation (frontend/builtin-fold.h), as the C++ front end does. */
_Static_assert(__builtin_abs(-3) == 3, "abs");
_Static_assert(__builtin_abs(7) == 7, "abs of a positive");
_Static_assert(__builtin_labs(-3000000000L) == 3000000000L, "labs");
_Static_assert(__builtin_llabs(-5000000000LL) == 5000000000LL, "llabs");
enum { A = __builtin_abs(-4) };
static int table[__builtin_abs(-5)];
static int global = __builtin_abs(-6);
#if defined(__has_constexpr_builtin)
_Static_assert(__has_constexpr_builtin(__builtin_abs), "claim");
_Static_assert(__has_constexpr_builtin(__builtin_llabs), "claim");
#endif

/* the string comparisons of literals fold too (their results are -1, 0, 1) */
_Static_assert(__builtin_strcmp("abc", "abd") < 0, "strcmp");
_Static_assert(__builtin_strcmp("abc", "abc") == 0, "strcmp equal");
_Static_assert(__builtin_strncmp("abcx", "abcy", 3) == 0, "strncmp");
_Static_assert(__builtin_memcmp("a\0b", "a\0c", 3) < 0, "memcmp");
static int compare_table[2 + __builtin_strcmp("a", "b")];

int main(void)
{
    volatile int runtime = -9;
    if (A != 4 || sizeof table != 5 * sizeof(int) || global != 6 ||
        sizeof compare_table != 1 * sizeof(int))
        return 1;
    if (__builtin_abs(runtime) != 9)
        return 2;
    return 0;
}
