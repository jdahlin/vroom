/* builtins with a C body (builtin-body.cpp): the type of the argument is the
   body's T.  Results at every unsigned width, zero, one evaluation, the
   constants clang folds. */
_Static_assert(__builtin_stdc_count_ones(7u) == 3u, "");
_Static_assert(__builtin_stdc_count_ones(~0ull) == 64u, "");
_Static_assert(__builtin_stdc_count_ones((unsigned char)255) == 8u, "");
_Static_assert(__builtin_stdc_trailing_zeros(8u) == 3u, "");
_Static_assert(__builtin_stdc_trailing_zeros(0u) == 32u, "");
_Static_assert(__builtin_stdc_trailing_zeros((unsigned char)0) == 8u, "");
_Static_assert(__builtin_stdc_trailing_zeros((unsigned short)0) == 16u, "");
_Static_assert(__builtin_stdc_trailing_zeros(0ull) == 64u, "");
_Static_assert(__builtin_stdc_trailing_zeros((unsigned __int128)0) == 128u, "");
_Static_assert(_Generic(__builtin_stdc_count_ones(3u), unsigned int: 1, default: 0), "type");
_Static_assert(_Generic(__builtin_stdc_trailing_zeros(3ull), unsigned int: 1, default: 0), "type");

static int calls;
static unsigned once(unsigned v)
{
    calls++;
    return v;
}

int main(void)
{
    volatile unsigned char b = 0x81, bz = 0;
    volatile unsigned short s = 0x8100, sz = 0;
    volatile unsigned u = 0x80000001u, uz = 0;
    volatile unsigned long long q = 0x8000000000000100ull, qz = 0;
    volatile unsigned __int128 w = ((unsigned __int128)1 << 100) | 4, wz = 0;

    if (__builtin_stdc_count_ones(b) != 2 || __builtin_stdc_trailing_zeros(b) != 0)
        return 1;
    if (__builtin_stdc_count_ones(bz) != 0 || __builtin_stdc_trailing_zeros(bz) != 8)
        return 2;
    if (__builtin_stdc_count_ones(s) != 2 || __builtin_stdc_trailing_zeros(s) != 8)
        return 3;
    if (__builtin_stdc_trailing_zeros(sz) != 16)
        return 4;
    if (__builtin_stdc_count_ones(u) != 2 || __builtin_stdc_trailing_zeros(u) != 0)
        return 5;
    if (__builtin_stdc_trailing_zeros(uz) != 32)
        return 6;
    if (__builtin_stdc_count_ones(q) != 2 || __builtin_stdc_trailing_zeros(q) != 8)
        return 7;
    if (__builtin_stdc_trailing_zeros(qz) != 64 || __builtin_stdc_count_ones(qz) != 0)
        return 8;
    if (__builtin_stdc_count_ones(w) != 2 || __builtin_stdc_trailing_zeros(w) != 2)
        return 9;
    if (__builtin_stdc_trailing_zeros(wz) != 128)
        return 10;
    if (__builtin_stdc_count_ones(once(7)) != 3 || calls != 1)
        return 11;
    if (__builtin_stdc_trailing_zeros(once(0)) != 32 || calls != 2)
        return 12;
    return 0;
}
