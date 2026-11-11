/* vcc-xfail-wasm: TODO.md wasm: Linux-correctness coverage gaps: no inliner for object-size bounds of call arguments */
typedef __SIZE_TYPE__ size_t;
static inline __attribute__((always_inline)) size_t bound(char *p)
{
    return __builtin_object_size(p, 1);
}
static inline __attribute__((always_inline)) size_t changed(char *p, char *q)
{
    p = q;
    return __builtin_object_size(p, 1);
}
static inline __attribute__((always_inline)) int is_five(char *p)
{ return __builtin_object_size(p, 1) == 5; }
static int calls;
static __attribute__((noinline)) char *unknown(char *p) { calls++; return p; }
struct outer {
    union {
        struct { int a,b; char c,d; } small;
        struct { int a,b; char c,d[255]; } large;
    } u;
};
int main(void)
{
    char a[5], b[9]; struct outer o;
    if (bound(a) != 5 || bound(b) != 9 || bound(a+2) != 3) return 1;
    if (!is_five(a) || is_five(b)) return 4;
    if (bound(o.u.large.d) != 255) return 2;
    /* A written parameter must not inherit p's old bound. */
    if (changed(a, unknown(b)) == 5 || calls != 1) return 3;
    return 0;
}
