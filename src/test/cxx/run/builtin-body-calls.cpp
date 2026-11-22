/* [ext:gnu_library_builtins] builtins with a C body (builtin-body.cpp) called
   from C++ with run-time operands: the carry chains, the floating
   classifications and C23's bit counts.  One evaluation of each argument;
   the constants fold where clang folds them. */
static_assert(__builtin_iszero(0.0), "");
static_assert(__builtin_iszero(-0.0f), "");
static_assert(!__builtin_iszero(1.0), "");
static_assert(__builtin_issubnormal(__DBL_DENORM_MIN__), "");
static_assert(!__builtin_issubnormal(1.0f), "");
static_assert(__builtin_stdc_count_ones(7u) == 3u, "");
static_assert(__builtin_stdc_trailing_zeros(0u) == 32u, "");

static int calls;
template <class T>
static T once(T v)
{
    calls++;
    return v;
}

template <class T>
static int classes(T zero, T tiny, T one)
{
    if (!__builtin_iszero(zero) || !__builtin_iszero(-zero))
        return 1;
    if (__builtin_iszero(tiny) || __builtin_iszero(one))
        return 2;
    if (!__builtin_issubnormal(tiny) || !__builtin_issubnormal(-tiny))
        return 3;
    if (__builtin_issubnormal(zero) || __builtin_issubnormal(one))
        return 4;
    return 0;
}

int main()
{
    volatile unsigned vu = 0x80000100u;
    volatile unsigned long long vq = 0;
    unsigned c;
    unsigned long long cll;

    if (__builtin_addc(0xffffffffu, 1u, 0u, &c) != 0 || c != 1)
        return 1;
    if (__builtin_addc(0xffffffffu, 0xffffffffu, 1u, &c) != 0xffffffffu || c != 1)
        return 2;
    if (__builtin_subc(0u, 1u, 0u, &c) != 0xffffffffu || c != 1)
        return 3;
    if (__builtin_subc(5u, 3u, 1u, &c) != 1 || c != 0)
        return 4;
    if (__builtin_addcll(~0ull, ~0ull, 1ull, &cll) != ~0ull || cll != 1)
        return 5;
    if (__builtin_subcll(0ull, 0ull, 1ull, &cll) != ~0ull || cll != 1)
        return 6;
    unsigned char cb;
    if (__builtin_addcb(200, 55, 1, &cb) != 0 || cb != 1)
        return 7;
    unsigned short cs;
    if (__builtin_subcs(0, 1, 0, &cs) != 0xffff || cs != 1)
        return 8;
    unsigned long cl;
    if (__builtin_addcl(~0ul, 1ul, 0ul, &cl) != 0 || cl != 1)
        return 9;
    if (__builtin_subcl(0ul, 1ul, 1ul, &cl) != ~0ul - 1 || cl != 1)
        return 10;
    c = 1;
    if (__builtin_addc(0xffffffffu, 0u, c, &c) != 0 || c != 1)
        return 11;

    if (once(__builtin_addcll(once(1ull), once(2ull), once(3ull), &cll)) != 6 || calls != 4)
        return 12;

    if (classes<float>(0.0f, __FLT_DENORM_MIN__, 1.0f))
        return 13;
    if (classes<double>(0.0, __DBL_DENORM_MIN__, 1.0))
        return 14;
    if (classes<long double>(0.0L, __LDBL_DENORM_MIN__, 1.0L))
        return 15;
    calls = 0;
    if (!__builtin_iszero(once(0.0)) || calls != 1)
        return 16;
    if (__builtin_issubnormal(once(1.0)) || calls != 2)
        return 17;

    if (__builtin_stdc_count_ones(vu) != 2 || __builtin_stdc_trailing_zeros(vu) != 8)
        return 18;
    if (__builtin_stdc_trailing_zeros(vq) != 64 || __builtin_stdc_count_ones(vq) != 0)
        return 19;
    calls = 0;
    if (__builtin_stdc_count_ones(once(7u)) != 3 || calls != 1)
        return 20;
    if (__builtin_stdc_trailing_zeros(static_cast<unsigned char>(once(0u))) != 8 || calls != 2)
        return 21;
    return 0;
}
