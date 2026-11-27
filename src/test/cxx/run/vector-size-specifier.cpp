// [ext:gnu_vector_size] vector_size among the decl-specifiers: the type the
// specifiers name becomes the vector, declarators build on that
extern "C" int printf(const char *, ...);

typedef int __attribute__((vector_size(16))) I4, *PI4, A2[2];
typedef const float __attribute__((vector_size(8))) CF2;
static_assert(sizeof(I4) == 16);
static_assert(sizeof(*(PI4)0) == 16);
static_assert(sizeof(A2) == 32);
static_assert(sizeof(CF2) == 8);

static int sum(I4 v)
{
    return v[0] + v[1] + v[2] + v[3];
}

template <class T> static int local(T x)
{
    typedef short __attribute__((vector_size(16))) S8;
    S8 s = {1, 2, 3, 4, 5, 6, 7, 8};
    s += static_cast<short>(x);
    return s[0] + s[7] + static_cast<int>(sizeof(s));
}

int main()
{
    I4 a = {1, 2, 3, 4};
    PI4 p = &a;
    A2 two = {{1, 1, 1, 1}, {2, 2, 2, 2}};
    CF2 f = {1.5f, 2.5f};
    printf("%d %d %d %g %d\n", sum(a), sum(*p + two[1]), sum(two[0]),
           f[0] + f[1], local(3));
}
