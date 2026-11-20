/* clang's type-generic byte swap: __builtin_bswapg(x) of any integer type
   whose width is a multiple of 8 (clang 21's Builtins.td, custom type
   checking); libc++'s <bit> and <__bit/byteswap.h> use it. */
static_assert(__builtin_bswapg((unsigned char)0x12) == 0x12);
static_assert(__builtin_bswapg((unsigned short)0x1234) == 0x3412);
static_assert(__builtin_bswapg(0x12345678u) == 0x78563412u);
static_assert(__builtin_bswapg(0x0102030405060708ull) == 0x0807060504030201ull);
static_assert(__builtin_bswapg((signed char)-1) == -1);
static_assert(__builtin_bswapg(true) == true);

template <class T> constexpr T swap(T v)
{
    return __builtin_bswapg(v);
}

static_assert(swap<unsigned>(1u) == 0x01000000u);
static_assert(swap<unsigned long>(1ul) == 0x0100000000000000ul);

int main()
{
    volatile unsigned short s = 0xaabb;
    if (swap<unsigned short>(s) != 0xbbaa)
        return 1;
    volatile unsigned long long l = 0x1122334455667788ull;
    if (swap<unsigned long long>(l) != 0x8877665544332211ull)
        return 2;
    return 0;
}
