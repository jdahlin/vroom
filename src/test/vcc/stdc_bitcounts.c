_Static_assert(_Generic(__builtin_stdc_count_ones(3u), unsigned int: 1, default: 0), "type");
_Static_assert(__builtin_stdc_trailing_zeros((unsigned _BitInt(77))0) == 77, "zero width");
_Static_assert(__builtin_stdc_count_ones((unsigned char)255) == 8, "population");

int main(void)
{
    volatile unsigned char x = 0;
    if (__builtin_stdc_trailing_zeros(x++) != 8 || x != 1) return 1;
    if (__builtin_stdc_count_ones(x++) != 1 || x != 2) return 2;
    return __builtin_stdc_trailing_zeros(x) != 1;
}
