/* [ext:clang_builtin_functions] clang's carry chains: unsigned
   __builtin_addc(x, y, carry_in, &carry_out) and __builtin_subc, with the
   b, s, l and ll typed forms (clang language extensions, "Multiprecision
   Arithmetic Builtins"). */
int main()
{
    unsigned c;
    if (__builtin_addc(0xffffffffu, 1u, 0u, &c) != 0 || c != 1)
        return 1;
    if (__builtin_addc(1u, 2u, 1u, &c) != 4 || c != 0)
        return 2;
    if (__builtin_subc(0u, 1u, 0u, &c) != 0xffffffffu || c != 1)
        return 3;
    if (__builtin_subc(5u, 3u, 1u, &c) != 1 || c != 0)
        return 4;
    unsigned long cl;
    if (__builtin_addcl(~0ul, 1ul, 0ul, &cl) != 0 || cl != 1)
        return 5;
    unsigned long long cll;
    if (__builtin_subcll(0ull, 0ull, 1ull, &cll) != ~0ull || cll != 1)
        return 6;
    unsigned char cb;
    if (__builtin_addcb(255, 1, 0, &cb) != 0 || cb != 1)
        return 7;
    unsigned short cs;
    if (__builtin_subcs(0, 1, 0, &cs) != 0xffff || cs != 1)
        return 8;
    /* a 128-bit addition by two limbs */
    unsigned long long lo, hi, k;
    lo = __builtin_addcll(~0ull, 2ull, 0ull, &k);
    hi = __builtin_addcll(0ull, 0ull, k, &k);
    if (lo != 1 || hi != 1 || k != 0)
        return 9;
    return 0;
}
