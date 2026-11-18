// [ext:clang_nullability] _Nonnull, _Nullable, _Nullable_result and
// _Null_unspecified as cv-qualifiers that leave the type as it is (the
// SDK's <_stdlib.h>, libc++'s construct_at).  [temp.names]/2: a variable's
// name followed by < is no template-name, so (c < 0 || c >= 256) is a
// parenthesized expression, not a cast ([dcl.ambig.res]/2; the SDK's
// <_ctype.h>).
int first(const int *_Nonnull p, int *_Nullable q) { return q ? *q : *p; }
int *_Null_unspecified pass(int *_Nullable_result p) { return p; }
static int limit = 3;
inline int classify(int c, unsigned long f)
{
    return (c < 0 || c >= (1 << 8)) ? 0 : !!(c & f);
}
int main()
{
    int a = 4, b = 5;
    if (first(&a, nullptr) != 4 || first(&a, &b) != 5) return 1;
    if (pass(&a) != &a) return 2;
    if (classify(-1, 1) != 0 || classify(300, 1) != 0) return 3;
    if (classify(3, 1) != 1 || classify(2, 1) != 0) return 4;
    if ((limit < 4) != 1 || 1 + (limit < 2) != 1) return 5;
    return 0;
}
