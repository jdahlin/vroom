/* [ext:gnu_vector_size] __builtin_shufflevector(a, b, indices...): lanes of
   a then b picked by constant indices, as many lanes as indices. */
typedef int v4si __attribute__((vector_size(16)));
typedef int v2si __attribute__((vector_size(8)));
typedef int v8si __attribute__((vector_size(32)));
typedef float v4sf __attribute__((vector_size(16)));
typedef short v8hi __attribute__((vector_size(16)));

int main()
{
    v4si a = {1, 2, 3, 4}, b = {10, 20, 30, 40};
    v4si rev = __builtin_shufflevector(a, a, 3, 2, 1, 0);
    v4si mix = __builtin_shufflevector(a, b, 0, 4, 1, 5);
    v4si bcast = __builtin_shufflevector(b, b, 2, 2, 2, 2);
    v2si low = __builtin_shufflevector(a, b, 0, 7);
    v8si wide = __builtin_shufflevector(a, b, 0, 1, 2, 3, 4, 5, 6, 7);
    if (rev[0] != 4 || rev[3] != 1 || mix[1] != 10 || mix[2] != 2 ||
        mix[3] != 20)
        return 1;
    if (bcast[0] != 30 || bcast[3] != 30 || low[0] != 1 || low[1] != 40)
        return 2;
    if (wide[0] != 1 || wide[3] != 4 || wide[4] != 10 || wide[7] != 40)
        return 3;
    v4sf f = {1.5f, 2.5f, 3.5f, 4.5f};
    v4sf swapped = __builtin_shufflevector(f, f, 2, 3, 0, 1);
    v8hi h = {1, 2, 3, 4, 5, 6, 7, 8};
    v8hi hh = __builtin_shufflevector(h, h, 7, 6, 5, 4, 3, 2, 1, 0);
    if (swapped[0] != 3.5f || swapped[3] != 2.5f || hh[0] != 8 || hh[7] != 1)
        return 4;
    return 0;
}
