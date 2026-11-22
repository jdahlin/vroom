/* [ext:gnu_library_builtins] C23's <stdbit.h> counts as clang spells them:
   __builtin_stdc_count_ones and __builtin_stdc_trailing_zeros fold in
   constant expressions (the zero value's trailing zeros are the width). */
static_assert(__builtin_stdc_count_ones(7u) == 3u, "");
static_assert(__builtin_stdc_count_ones(0xffffffffffffffffull) == 64u, "");
static_assert(__builtin_stdc_trailing_zeros(8u) == 3u, "");
static_assert(__builtin_stdc_trailing_zeros(0u) == 32u, "");
static_assert(__builtin_stdc_trailing_zeros((unsigned char)0) == 8u, "");

/* (a call with a run-time operand is not lowered yet: not-compiled in
   src/test/builtins/consistency-failures.txt) */
int main()
{
    return 0;
}
