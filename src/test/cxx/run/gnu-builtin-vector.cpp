/* [ext:clang_vector_builtins] clang's vector builtins on GCC vector types
   (__attribute__((vector_size))): __builtin_convertvector,
   __builtin_shufflevector, __builtin_elementwise_* and __builtin_reduce_*
   (clang language extensions, "Vectors and Extended Vectors"; "Builtin
   Functions"); libc++'s <__algorithm/simd_utils.h> uses them. */
typedef int v4i __attribute__((vector_size(16)));
typedef unsigned v4u __attribute__((vector_size(16)));
typedef float v4f __attribute__((vector_size(16)));
typedef char v16c __attribute__((vector_size(16)));

static v4f to_float(v4i x)
{
    return __builtin_convertvector(x, v4f);
}
static v4i to_int(v4f x)
{
    return __builtin_convertvector(x, v4i);
}
static v4i interleave(v4i a, v4i b)
{
    return __builtin_shufflevector(a, b, 0, 4, 1, 5);
}
static v4i reverse(v4i a)
{
    return __builtin_shufflevector(a, a, 3, 2, 1, 0);
}
static v4i add_sat(v4i a, v4i b)
{
    return __builtin_elementwise_add_sat(a, b);
}
static v4i sub_sat(v4i a, v4i b)
{
    return __builtin_elementwise_sub_sat(a, b);
}
static v4i elementwise_max(v4i a, v4i b)
{
    return __builtin_elementwise_max(a, b);
}
static int total(v4i a)
{
    return __builtin_reduce_add(a);
}

int main()
{
    v4i x = {1, -2, 3, -4};
    v4f f = to_float(x);
    if (f[1] != -2.0f || f[3] != -4.0f)
        return 1;
    v4f g = {1.9f, -1.9f, 2.5f, 0.0f};
    v4i h = to_int(g);
    if (h[0] != 1 || h[1] != -1 || h[2] != 2)
        return 2;
    v4i y = {10, 20, 30, 40};
    v4i z = interleave(x, y);
    if (z[0] != 1 || z[1] != 10 || z[2] != -2 || z[3] != 20)
        return 3;
    v4i r = reverse(x);
    if (r[0] != -4 || r[3] != 1)
        return 4;
    v4i big = {2147483647, 1, -2147483647 - 1, 0};
    v4i one = {1, 1, -1, 0};
    v4i s = add_sat(big, one);
    if (s[0] != 2147483647 || s[1] != 2 || s[2] != -2147483647 - 1)
        return 5;
    v4i d = sub_sat(big, one);
    if (d[0] != 2147483646 || d[2] != -2147483647 - 1 + 1)
        return 6;
    v4i m = elementwise_max(x, y);
    if (m[0] != 10 || m[1] != 20)
        return 7;
    if (total(x) != -2)
        return 8;
    if (__builtin_reduce_max(y) != 40 || __builtin_reduce_min(y) != 10)
        return 9;
    v4i masks = {0xff, 0x0f, 0x3c, 0xf0};
    if (__builtin_reduce_and(masks) != 0 || __builtin_reduce_or(masks) != 0xff ||
        __builtin_reduce_xor(masks) != (0xff ^ 0x0f ^ 0x3c ^ 0xf0))
        return 10;
    return 0;
}
