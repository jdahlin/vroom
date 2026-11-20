/* [ext:gnu_library_builtins] __builtin_add_overflow, __builtin_sub_overflow
   and __builtin_mul_overflow (GCC manual 6.56): the operands and the
   result of any integer types, and the typed forms. */
static int check_int()
{
    int r;
    if (__builtin_add_overflow(1, 2, &r) || r != 3)
        return 1;
    if (!__builtin_add_overflow(2147483647, 1, &r) || r != -2147483647 - 1)
        return 2;
    if (__builtin_sub_overflow(5, 7, &r) || r != -2)
        return 3;
    if (!__builtin_mul_overflow(65536, 65536, &r) || r != 0)
        return 4;
    return 0;
}

/* mixed operand and result types: the result is the exact value
   converted to the pointee's type */
static int check_mixed()
{
    unsigned char c;
    if (!__builtin_add_overflow(200, 100, &c) || c != 44)
        return 1;
    unsigned u;
    if (!__builtin_sub_overflow(0u, 1, &u) || u != 0xffffffffu)
        return 2;
    long long ll;
    if (__builtin_mul_overflow(1 << 20, 1L << 30, &ll) || ll != 1LL << 50)
        return 3;
    unsigned long ul;
    if (!__builtin_add_overflow(-1, 0, &ul) || ul != ~0ul)
        return 4;
    short s;
    if (__builtin_add_overflow((signed char)100, (unsigned char)27, &s) ||
        s != 127)
        return 5;
    return 0;
}

static int check_typed()
{
    int i;
    unsigned long long ull;
    long l;
    if (__builtin_sadd_overflow(1, 2, &i) || i != 3)
        return 1;
    if (!__builtin_sadd_overflow(2147483647, 1, &i))
        return 2;
    if (!__builtin_usubll_overflow(1ull, 2ull, &ull) || ull != ~0ull)
        return 3;
    if (__builtin_smull_overflow(3, 4, &l) || l != 12)
        return 4;
    /* an operand converted to the builtin's type */
    unsigned u;
    if (!__builtin_uadd_overflow(-1, 1, &u) || u != 0)
        return 5;
    return 0;
}

/* clang accepts bool and enumeration operands, an enumeration result */
enum Color { Red = 1, Blue = 255 };
enum class Wide : unsigned long long { Big = ~0ull };

static int check_bool_enum()
{
    int r;
    if (__builtin_add_overflow(true, true, &r) || r != 2)
        return 1;
    if (__builtin_add_overflow(Red, Blue, &r) || r != 256)
        return 3;
    unsigned char c;
    if (!__builtin_add_overflow(Red, Blue, &c) || c != 0)
        return 4;
    Color k;
    if (__builtin_add_overflow(Red, 1, &k) || k != 2)
        return 5;
    return 0;
}

/* the value is a bool usable in conditions */
static bool wraps(unsigned a, unsigned b)
{
    unsigned r;
    return __builtin_mul_overflow(a, b, &r);
}

static unsigned long checked_size(unsigned long n, unsigned long m)
{
    unsigned long r;
    if (__builtin_mul_overflow(n, m, &r))
        return ~0ul;
    return r;
}

int main()
{
    if (int r = check_int())
        return r;
    if (int r = check_mixed())
        return 10 + r;
    if (int r = check_typed())
        return 20 + r;
    if (int r = check_bool_enum())
        return 40 + r;
    if (wraps(1000, 1000) || !wraps(100000, 100000))
        return 30;
    if (checked_size(1ul << 40, 1ul << 30) != ~0ul ||
        checked_size(1ul << 20, 3) != (3ul << 20))
        return 31;
    return 0;
}
