/* clang's __attribute__((enable_if(cond, "msg"))) with a non-identifier
   constant-true condition such as 1 or 1 + 1 == 2 (glibc _FORTIFY_SOURCE's
   __fortify_clang_prefer_this_overload: enable_if(1, "")). */

#define FORTIFY_INLINE \
    extern inline __attribute__((__always_inline__, __gnu_inline__))

FORTIFY_INLINE int pick_int(int x) { return x + 1; }
__attribute__((enable_if(1, ""))) FORTIFY_INLINE int pick_int(int x)
{
    return x + 10;
}

FORTIFY_INLINE int pick_expr(int x) { return x + 2; }
__attribute__((enable_if(1 + 1 == 2, "preferred"))) FORTIFY_INLINE int
pick_expr(int x)
{
    return x + 20;
}

FORTIFY_INLINE int pick_true(int x) { return x + 3; }
__attribute__((enable_if(true, ""))) FORTIFY_INLINE int pick_true(int x)
{
    return x + 30;
}

FORTIFY_INLINE int trail_fn(int x) __attribute__((enable_if(1, "")))
{
    return x + 40;
}

int main()
{
    if (pick_int(5) != 15)
        return 1;
    if (pick_expr(5) != 25)
        return 2;
    if (pick_true(5) != 35)
        return 3;
    if (trail_fn(5) != 45)
        return 4;
    return 0;
}
