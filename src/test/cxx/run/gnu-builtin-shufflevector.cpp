/* [ext:clang_convertvector] __builtin_convertvector and
   __builtin_shufflevector on GCC vector types (clang language extensions,
   "Vectors and Extended Vectors"); libc++'s <__algorithm/simd_utils.h>
   uses them. */
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
    return 0;
}
