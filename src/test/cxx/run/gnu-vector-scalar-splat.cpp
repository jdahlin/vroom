/* [ext:gnu_vector_size] a scalar operand converted to the element type
   where clang finds no truncation: a short to float, an int and a float to
   double, -1 to unsigned, constants the element holds exactly. */
typedef float v4sf __attribute__((vector_size(16)));
typedef double v2df __attribute__((vector_size(16)));
typedef unsigned v4su __attribute__((vector_size(16)));

template <class V, class E> static E lane(V v, int i)
{
    E e[16 / sizeof(E)];
    __builtin_memcpy(e, &v, sizeof v);
    return e[i];
}

static v4sf fs(v4sf v, short s) { return v - s; }
static v2df di(v2df v, int x) { return v / x; }
static v2df df(v2df v, float f) { return v + f; }
static v4su um(v4su v) { return v + -1; }
static v4sf fc(v4sf v) { return v * 0.5 + 16777216; }

int main()
{
    v4sf f = {1, 2, 3, 4};
    v2df d = {9, 12};
    v4su u = {1, 2, 3, 4};
    if (lane<v4sf, float>(fs(f, 3), 3) != 1)
        return 1;
    if (lane<v2df, double>(di(d, 3), 1) != 4)
        return 2;
    if (lane<v2df, double>(df(d, 0.25f), 0) != 9.25)
        return 3;
    if (lane<v4su, unsigned>(um(u), 0) != 0)
        return 4;
    if (lane<v4sf, float>(fc(f), 3) != 16777218)
        return 5;
    return 0;
}
