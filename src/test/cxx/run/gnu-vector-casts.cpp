/* [ext:gnu_vector_size] a cast between a vector and a vector or a scalar of
   the same size reinterprets the bits; __builtin_convertvector converts the
   elements. */
typedef int v4si __attribute__((vector_size(16)));
typedef unsigned v4su __attribute__((vector_size(16)));
typedef float v4sf __attribute__((vector_size(16)));
typedef short v8hi __attribute__((vector_size(16)));
typedef long long v2di __attribute__((vector_size(16)));
typedef int v2si __attribute__((vector_size(8)));

int main()
{
    v4sf f;
    f[0] = 1.0f;
    f[1] = -2.0f;
    f[2] = 0.5f;
    f[3] = 3.0f;
    v4si bits = (v4si)f;
    if (bits[0] != 0x3f800000 || bits[1] != (int)0xc0000000)
        return 1;
    v4sf back = (v4sf)bits;
    if (back[2] != 0.5f || back[3] != 3.0f)
        return 2;
    v4su u = (v4su)bits;
    v8hi h = (v8hi)bits;
    v2di q = static_cast<v2di>(u);
    if (u[0] != 0x3f800000u || h[1] != 0x3f80 || q[0] != 0xc00000003f800000ll)
        return 3;
    v2si small = {1, 2};
    long long wide = (long long)small;
    v2si again = (v2si)wide;
    if (wide != 0x200000001ll || again[0] != 1 || again[1] != 2)
        return 4;
    __int128 big = (__int128)bits;
    v4si round = (v4si)big;
    if (round[3] != bits[3])
        return 5;
    v4si ints = {1, -2, 3, 40};
    v4sf conv = __builtin_convertvector(ints, v4sf);
    v4si trunc = __builtin_convertvector(f * 3.0f, v4si);
    if (conv[1] != -2.0f || conv[3] != 40.0f || trunc[1] != -6 || trunc[2] != 1)
        return 6;
    return 0;
}
