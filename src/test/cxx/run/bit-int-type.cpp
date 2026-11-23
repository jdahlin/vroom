/* [ext:clang_bit_int] _BitInt(N) as a type-specifier: its layout as clang
   gives it on AArch64, and a function template whose parameter is
   _BitInt(N) of its template-parameter N (libc++'s __math/abs.h), which a
   call with an int does not deduce. */
template <int N> int width_of(_BitInt(N) x)
{
    return N;
}
int width_of(int x)
{
    return 32;
}

struct holder {
    char tag;
    _BitInt(65) wide;
};

static_assert(sizeof(_BitInt(2)) == 1 && alignof(_BitInt(9)) == 2, "");
static_assert(sizeof(_BitInt(33)) == 8 && sizeof(_BitInt(100)) == 16, "");
static_assert(alignof(_BitInt(128)) == 16 && sizeof(holder) == 32, "");

int main()
{
    return width_of(7) == 32 ? 0 : 1;
}
