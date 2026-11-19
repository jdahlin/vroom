/* [ext:clang_bit_cast] __builtin_bit_cast ([bit.cast]): the object
   representation as another type, folded in constant expressions,
   copied at run time (libc++'s std::bit_cast). */
template <class To, class From> constexpr To bit_cast(const From &from)
{
    return __builtin_bit_cast(To, from);
}

static_assert(__builtin_bit_cast(unsigned, 1.0f) == 0x3f800000u, "");
static_assert(__builtin_bit_cast(unsigned long long, 2.0) ==
                  0x4000000000000000ull,
              "");
static_assert(__builtin_bit_cast(float, 0x40400000) == 3.0f, "");
static_assert(__builtin_bit_cast(unsigned char, (signed char)-1) == 255, "");

int main()
{
    float f = -0.5f;
    unsigned u = __builtin_bit_cast(unsigned, f);
    if (u != 0xbf000000u)
        return 1;
    double d = __builtin_bit_cast(double, 0x3ff0000000000000ull);
    if (d != 1.0)
        return 2;
    long l = 42;
    long *p = &l;
    unsigned long a = __builtin_bit_cast(unsigned long, p);
    if (*__builtin_bit_cast(long *, a) != 42)
        return 3;
    if (bit_cast<unsigned>(f) != u)
        return 4;
    return 0;
}
