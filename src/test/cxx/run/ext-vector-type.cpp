/* clang's ext_vector_type after the type-specifier (Skia's SkVx.h):
   operators, element access, conversions and template mangling */
extern "C" int printf(const char *, ...);
template <int N, typename T> using V = T __attribute__((ext_vector_type(N)));
typedef float __attribute__((ext_vector_type(4))) float4;

template <int N, typename T> V<N, T> add(V<N, T> a, V<N, T> b)
{
    return a + b;
}

template <int N, typename T> struct ops {
    static V<N, T> mix(V<N, T> a, V<N, T> b)
    {
        V<N, T> r = (a * b - a) ^ (b & a);
        r += a | b;
        r = -r;
        r = ~r;
        return (r << 1) >> 1;
    }
};

float sum(float4 v) { return v[0] + v[1] + v[2] + v[3]; }

int main()
{
    V<4, int> a = {1, 2, 3, 4}, b = {4, 3, 2, 1};
    V<4, int> c = add<4, int>(a, b);
    V<4, int> m = ops<4, int>::mix(a, b);
    auto less = a < b;
    a[2] = 30;
    float4 f = __builtin_convertvector(a, float4);
    float4 g = f * 2.0f + f / f;
    auto s = __builtin_shufflevector(g, g, 3, 2, 1, 0);
    V<4, unsigned> u = (V<4, unsigned>)less;
    V<2, double> d = {1.5, 2.5};
    d = d * d;
    printf("%d %d %d %d\n", c[0], c[3], a[2], (int)sizeof(a));
    printf("%d %d %d %d\n", m[0], m[1], m[2], m[3]);
    printf("%d %d %u %u\n", less[0], less[3], u[1], u[2]);
    printf("%g %g %g %g %g\n", sum(f), sum(g), s[0], s[3], d[0] + d[1]);
    return 0;
}
