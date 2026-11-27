/* [ext:clang_ext_vector_type] an integer that is no constant beside an
   extended vector of integers is converted to the element type */
extern "C" int printf(const char *, ...);

typedef unsigned U __attribute__((ext_vector_type(4)));
typedef unsigned char C __attribute__((ext_vector_type(4)));
typedef short S __attribute__((ext_vector_type(4)));

static U index(U y, U x, const int *stride) { return y * *stride + x; }

static void show(const char *name, int a, int b, int c, int d)
{
    printf("%s %d %d %d %d\n", name, a, b, c, d);
}

int main(int argc, char **)
{
    int stride = 1000 * argc;
    U r = index(U{0, 1, 2, 3}, U{4, 5, 6, 7}, &stride);
    show("u", r[0], r[1], r[2], r[3]);
    long wide = 256 + 3 * argc;
    C c = {10, 100, 200, 255};
    C q = c / wide, m = wide - c;
    show("c/", q[0], q[1], q[2], q[3]);
    show("c-", m[0], m[1], m[2], m[3]);
    auto less = c < wide;
    show("c<", less[0], less[1], less[2], less[3]);
    unsigned long long big = 0x10000ull + 40000u * argc;
    S s = {-100, 0, 100, 30000};
    S t = s / big;
    show("s/", t[0], t[1], t[2], t[3]);
    return 0;
}
