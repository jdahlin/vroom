/* Fixed signatures convert before counting, including unsigned long on LLP64. */
_Static_assert(__builtin_popcount(~0u) == 32, "unsigned int width");
_Static_assert(__builtin_popcountl(1ULL << 40) == (sizeof(long) == 8),
               "unsigned long conversion");
_Static_assert(__builtin_popcountll(1ULL << 40) == 1, "unsigned long long width");

static volatile unsigned long long input = (1ULL << 40) | 3;
static int evaluations;

__attribute__((noinline)) static unsigned long long next(void)
{
    ++evaluations;
    return input;
}

int main(void)
{
    if (__builtin_popcount(next()) != 2)
        return 1;
    if (__builtin_popcountl(next()) != 2 + (sizeof(long) == 8))
        return 2;
    if (__builtin_popcountll(next()) != 3)
        return 3;
    return evaluations != 3;
}
