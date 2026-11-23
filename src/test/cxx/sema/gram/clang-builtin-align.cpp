// __builtin_align_up, __builtin_align_down and __builtin_is_aligned: the operand's type, clang++'s values
template <class T, class U> struct same { static constexpr bool value = false; };
template <class T> struct same<T, T> { static constexpr bool value = true; };
int *p;
void *v;
unsigned long n;
short s;
long long f(unsigned a)
{
    static_assert(same<decltype(__builtin_align_up(p, 16)), int *>::value);
    static_assert(same<decltype(__builtin_align_down(v, 8)), void *>::value);
    static_assert(same<decltype(__builtin_align_up(n, 4096u)), unsigned long>::value);
    static_assert(same<decltype(__builtin_align_down(s, 2)), short>::value);
    static_assert(same<decltype(__builtin_is_aligned(p, 4)), bool>::value);
    static_assert(same<decltype(__builtin_is_aligned(n, a)), bool>::value);
    return 0;
}

// the values of integer operands (clang++'s)
static_assert(__builtin_align_up(5, 4) == 8);
static_assert(__builtin_align_down(5, 4) == 4);
static_assert(__builtin_align_down(-5, 4) == -8);
static_assert(__builtin_align_up(-5, 4) == -4);
static_assert(!__builtin_is_aligned(5, 4));
static_assert(__builtin_is_aligned(-8, 4));
static_assert(__builtin_align_up(5u, 1) == 5u);
static_assert(__builtin_align_up((unsigned char)250, 16) == 0);
static_assert(__builtin_align_up((short)32760, 16) == -32768);
static_assert(__builtin_align_up(5L, 4u) == 8L);
static_assert(__builtin_align_up(5, 4LL) == 8);
static_assert(__builtin_align_up(5ull, 1ull << 62) == 1ull << 62);
static_assert(__builtin_align_up('a', 4) == 100);
static_assert(__builtin_align_up(5, 1u << 31) == 0x80000000u);
static_assert(!__builtin_is_aligned((char)5, 128));
static_assert(__builtin_is_aligned((signed char)-128, 128));
static_assert(__builtin_align_up(2147483647, 2) == -2147483647 - 1);
static_assert(same<decltype(__builtin_align_up('a', 4)), char>::value);
