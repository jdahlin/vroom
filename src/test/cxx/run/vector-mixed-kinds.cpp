// [ext:gnu_vector_size] [ext:clang_ext_vector_type] binary operators on a
// GCC vector and an extended vector of the same elements
typedef float G __attribute__((vector_size(16)));
typedef float E __attribute__((ext_vector_type(4)));
typedef int GI __attribute__((vector_size(16)));
typedef int EI __attribute__((ext_vector_type(4)));
extern "C" int printf(const char *, ...);

template <class A, class B> struct same {
    static const bool v = false;
};
template <class A> struct same<A, A> {
    static const bool v = true;
};

static void show(E v) { printf("%g %g %g %g\n", v[0], v[1], v[2], v[3]); }
static void showi(EI v) { printf("%d %d %d %d\n", v[0], v[1], v[2], v[3]); }

__attribute__((noinline)) static void run(G g, E e, GI gi, EI ei)
{
    static_assert(same<decltype(g * e), E>::v);
    static_assert(same<decltype(e - g), E>::v);
    static_assert(same<decltype(g += e), G &>::v);
    static_assert(same<decltype(e += g), E &>::v);
    static_assert(same<decltype(g < e), EI>::v);
    static_assert(same<decltype(e < g), EI>::v);
    show(g * e);
    show(g - e);
    show(e / g);
    showi(g < e);
    showi(e == g);
    showi(gi % ei);
    showi(ei << gi);
    G c = g;
    c += e;
    printf("%g %g %g %g\n", c[0], c[1], c[2], c[3]);
    E d = e;
    d -= g;
    show(d);
}

int main()
{
    G g = {1, 2, 3, 4};
    E e = {8, 2, 1, -4};
    GI gi = {7, 1, 3, 2};
    EI ei = {3, 5, 2, 7};
    run(g, e, gi, ei);
}
