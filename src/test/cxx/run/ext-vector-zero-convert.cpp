// [ext:clang_ext_vector_type] V() is zero; a GCC vector converts to the
// extended vector of the same elements and back
extern "C" int printf(const char *, ...);

typedef float E __attribute__((ext_vector_type(4)));
typedef float G __attribute__((vector_size(16)));
typedef int EI __attribute__((ext_vector_type(4)));

static void show(E v) { printf("%g %g %g %g\n", v[0], v[1], v[2], v[3]); }
static G twice(G g) { return g + g; }
static E back(G g) { return g; }

__attribute__((noinline)) static void run(G g, E e)
{
    E a = g;
    show(a);
    a = twice(e);
    show(a);
    show(back(g));
    show(twice(g));
    show(E() + 1);
    show(e - (E() + 0));
    EI z = EI();
    printf("%d %d\n", z[0], z[3]);
}

int main()
{
    G g = {1, 2, 3, 4};
    E e = {5, 6, 7, 8};
    run(g, e);
}
