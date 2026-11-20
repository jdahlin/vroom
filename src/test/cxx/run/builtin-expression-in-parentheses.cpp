/* [dcl.ambig.res]/2 and [expr.prim.paren]: an extension's expression
   keyword (__builtin_offsetof, __builtin_va_arg, __builtin_bit_cast,
   __builtin_convertvector, __is_same) begins no decl-specifier, so
   ( keyword ( ... ) ) is a parenthesized expression, as in the replay
   code's X macro: if ((k) != 3 && (offsetof(S, a) != offsetof(S, b) || ...)). */
struct S {
    int kind;
    int a;
    long b;
};

static int cmp(const S *s, int k)
{
    if ((k) != 3 &&
        (__builtin_offsetof(S, a) != __builtin_offsetof(S, b) || s->kind == 4))
        return 1;
    return 0;
}

static int args(int n, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    int v = (__builtin_va_arg(ap, int));
    int w = (__builtin_va_arg(ap, int) + 1);
    __builtin_va_end(ap);
    return v * 10 + w;
}

static int traits()
{
    return (__is_same(int, int)) + (__is_same(int, long)) * 2 +
           (__builtin_bit_cast(int, 3u)) * 4;
}

int main()
{
    S s = {0, 0, 0};
    if (cmp(&s, 1) != 1 || cmp(&s, 3) != 0)
        return 1;
    if (args(0, 2, 5) != 26)
        return 2;
    if (traits() != 13)
        return 3;
    if ((__builtin_offsetof(S, b)) != 8)
        return 4;
    return 0;
}
