/* [ext:gnu_vector_size] the binary operators on vectors: lane by lane, a
   scalar splatted, comparisons giving -1 or 0 in a signed integer vector.
   The lanes are read back through memcpy (v[i] is not analysed yet). */
typedef int v4si __attribute__((vector_size(16)));
typedef unsigned v4su __attribute__((vector_size(16)));
typedef float v4sf __attribute__((vector_size(16)));
typedef double v2df __attribute__((vector_size(16)));
typedef signed char v16qi __attribute__((vector_size(16)));
typedef short v8hi __attribute__((vector_size(16)));
typedef long v2di __attribute__((vector_size(16)));

template <class V, class E> static E lane(V v, int i)
{
    E e[16 / sizeof(E)];
    __builtin_memcpy(e, &v, sizeof v);
    return e[i];
}

template <class V> static V zero()
{
    V v;
    __builtin_memset(&v, 0, sizeof v);
    return v;
}

static int failures;
#define CHECK(...)                                                             \
    do {                                                                       \
        if (!(__VA_ARGS__))                                                        \
            failures |= 1 << __LINE__ % 30;                                    \
    } while (0)

v4si make4(int a, int b, int c, int d)
{
    int w[4] = {a, b, c, d};
    v4si v;
    __builtin_memcpy(&v, w, 16);
    return v;
}

int main()
{
    v4si a = make4(1, 2, 3, 4), b = make4(10, 20, 30, 40);
    v4si s = a + b, d = b - a, p = a * b, q = b / a, r = b % make4(7, 7, 7, 7);
    CHECK(lane<v4si, int>(s, 0) == 11 && lane<v4si, int>(s, 3) == 44);
    CHECK(lane<v4si, int>(d, 1) == 18 && lane<v4si, int>(p, 2) == 90);
    CHECK(lane<v4si, int>(q, 3) == 10 && lane<v4si, int>(r, 1) == 6);
    v4si x = a & make4(3, 3, 3, 3), y = a | b, z = a ^ b;
    CHECK(lane<v4si, int>(x, 2) == 3 && lane<v4si, int>(y, 1) == 22);
    CHECK(lane<v4si, int>(z, 0) == 11);
    v4si t = a + 5, u = 5 + a, w = a * 2, h = 100 - a;
    CHECK(lane<v4si, int>(t, 3) == 9 && lane<v4si, int>(u, 0) == 6);
    CHECK(lane<v4si, int>(w, 2) == 6 && lane<v4si, int>(h, 1) == 98);
    v4si l = a << 2, g = b >> 1, ls = a << a;
    CHECK(lane<v4si, int>(l, 1) == 8 && lane<v4si, int>(g, 3) == 20);
    CHECK(lane<v4si, int>(ls, 3) == 64);
    v4si lt = a < make4(2, 2, 2, 2), eq = a == make4(1, 0, 3, 0);
    v4si ne = a != 3, ge = a >= 3;
    CHECK(lane<v4si, int>(lt, 0) == -1 && lane<v4si, int>(lt, 1) == 0);
    CHECK(lane<v4si, int>(eq, 0) == -1 && lane<v4si, int>(eq, 1) == 0);
    CHECK(lane<v4si, int>(ne, 2) == 0 && lane<v4si, int>(ne, 3) == -1);
    CHECK(lane<v4si, int>(ge, 1) == 0 && lane<v4si, int>(ge, 2) == -1);

    float fa[4] = {1.5f, 2.5f, 3.5f, 4.5f};
    v4sf f;
    __builtin_memcpy(&f, fa, 16);
    v4sf fs = f + f, fm = f * 2.0f, fd = f / f;
    v4si fc = f > 2.0f;
    CHECK(lane<v4sf, float>(fs, 1) == 5.0f && lane<v4sf, float>(fm, 3) == 9.0f);
    CHECK(lane<v4sf, float>(fd, 0) == 1.0f);
    CHECK(lane<v4si, int>(fc, 0) == 0 && lane<v4si, int>(fc, 1) == -1);

    double da[2] = {1.25, -2.0};
    v2df dv;
    __builtin_memcpy(&dv, da, 16);
    v2di dc = dv < 0.0;
    CHECK(lane<v2df, double>(dv - dv, 1) == 0.0);
    CHECK(lane<v2di, long>(dc, 0) == 0 && lane<v2di, long>(dc, 1) == -1);

    v16qi c = zero<v16qi>();
    c = c + 3;
    v8hi hh = zero<v8hi>() - 1;
    v4su us = zero<v4su>() + 7u;
    v4su um = us * 3u - 1u;
    CHECK(lane<v16qi, signed char>(c, 15) == 3);
    CHECK(lane<v8hi, short>(hh, 7) == -1);
    CHECK(lane<v4su, unsigned>(um, 2) == 20u);
    v4si n1 = -a, n2 = ~a, n3 = +b;
    CHECK(lane<v4si, int>(n1, 2) == -3 && lane<v4si, int>(n2, 0) == -2);
    CHECK(lane<v4si, int>(n3, 3) == 40);
    v4sf nf = -f;
    CHECK(lane<v4sf, float>(nf, 1) == -2.5f);
    v4si k = a;
    k += b;
    k *= 2;
    k -= 1;
    k <<= 1;
    k ^= make4(0, 0, 0, 1);
    k %= 1000;
    CHECK(lane<v4si, int>(k, 0) == 42 && lane<v4si, int>(k, 3) == 175);
    v4sf kf = f;
    kf /= 2.0f;
    kf += f;
    CHECK(lane<v4sf, float>(kf, 2) == 5.25f);
    v4si kk = (k += 1);
    CHECK(lane<v4si, int>(kk, 0) == 43);
    return failures;
}
