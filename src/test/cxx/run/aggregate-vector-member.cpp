/* [dcl.init.aggr]/16: a clause of vector type initializes a vector member
   or element whole (Highway's Vec128<T>{raw}); scalars still elide braces */
typedef int V __attribute__((__vector_size__(16)));
struct S {
    V raw;
};
struct Two {
    V a, b;
};

static V twice(V v) { return v + v; }
template <class T> S wrap(T v) { return S{twice(v)}; }

int main()
{
    V v = {1, 2, 3, 4};
    S s{v};
    S t = wrap(v);
    Two two{v, twice(v)};
    V list[2] = {t.raw, v};
    S elided{5, 6, 7, 8};
    if (s.raw[3] != 4 || t.raw[3] != 8 || two.b[0] != 2 || list[1][1] != 2)
        return 1;
    return elided.raw[0] + elided.raw[3] != 13;
}
