// C23's <stdbit.h> builtins and __builtin_bitreverseg of values known only
// at run time, lowered through the C front end's bit counts
template <class T> struct check {
    static int run(T zero, T one, T top, T all, T odd)
    {
        int r = 0;
        const unsigned w = sizeof(T) * 8;
        if (__builtin_stdc_leading_zeros(zero) != w ||
            __builtin_stdc_leading_zeros(one) != w - 1 ||
            __builtin_stdc_leading_zeros(top) != 0)
            r |= 1;
        if (__builtin_stdc_leading_ones(all) != w ||
            __builtin_stdc_leading_ones(top) != 1 ||
            __builtin_stdc_leading_ones(zero) != 0)
            r |= 2;
        if (__builtin_stdc_trailing_ones(all) != w ||
            __builtin_stdc_trailing_ones(odd) != 2 ||
            __builtin_stdc_trailing_ones(zero) != 0)
            r |= 4;
        if (__builtin_stdc_first_leading_zero(all) != 0 ||
            __builtin_stdc_first_leading_zero(top) != 2 ||
            __builtin_stdc_first_leading_one(zero) != 0 ||
            __builtin_stdc_first_leading_one(one) != w)
            r |= 8;
        if (__builtin_stdc_first_trailing_zero(all) != 0 ||
            __builtin_stdc_first_trailing_zero(odd) != 3 ||
            __builtin_stdc_first_trailing_one(zero) != 0 ||
            __builtin_stdc_first_trailing_one(top) != w)
            r |= 16;
        if (__builtin_stdc_count_zeros(zero) != w ||
            __builtin_stdc_count_zeros(odd) != w - 2 ||
            __builtin_stdc_bit_width(zero) != 0 ||
            __builtin_stdc_bit_width(odd) != 2 ||
            __builtin_stdc_bit_width(top) != w)
            r |= 32;
        if (__builtin_stdc_has_single_bit(zero) ||
            !__builtin_stdc_has_single_bit(top) ||
            __builtin_stdc_has_single_bit(odd))
            r |= 64;
        if (__builtin_stdc_bit_floor(zero) != 0 ||
            __builtin_stdc_bit_floor(odd) != 2 ||
            __builtin_stdc_bit_floor(all) != top)
            r |= 128;
        if (__builtin_stdc_bit_ceil(zero) != 1 ||
            __builtin_stdc_bit_ceil(one) != 1 ||
            __builtin_stdc_bit_ceil(odd) != 4 ||
            __builtin_stdc_bit_ceil(top) != top ||
            __builtin_stdc_bit_ceil(static_cast<T>(top + 1)) != 0)
            r |= 256;
        if (__builtin_bitreverseg(one) != top ||
            __builtin_bitreverseg(top) != one ||
            __builtin_bitreverseg(all) != all)
            r |= 512;
        return r;
    }
};
template <class T> int test()
{
    volatile T zero = 0, one = 1, odd = 3;
    volatile T top = static_cast<T>(static_cast<T>(1) << (sizeof(T) * 8 - 1));
    volatile T all = static_cast<T>(~static_cast<T>(0));
    return check<T>::run(zero, one, top, all, odd);
}
int main()
{
    int r = test<unsigned char>();
    r |= test<unsigned short>() << 10;
    r |= test<unsigned>() << 20;
    if (test<unsigned long>() || test<unsigned long long>())
        r |= 1 << 30;
    return r;
}
