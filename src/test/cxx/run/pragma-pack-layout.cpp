/* #pragma pack caps the members' alignment ([ext.clang], GCC's) */
#pragma pack(push, 1)
struct P1 {
    char c;
    long l;
    short s;
};
struct alignas(16) Wide {
    unsigned long lo, hi;
};
union U1 {
    char c[3];
    int i;
};
#pragma pack(push, 2)
struct P2 {
    char c;
    int i;
};
#pragma pack(pop)
template <class T> struct Box {
    char c;
    T t;
};
#pragma pack(pop)
struct Natural {
    char c;
    long l;
};

static_assert(sizeof(P1) == 11 && alignof(P1) == 1);
static_assert(sizeof(Wide) == 16 && alignof(Wide) == 16);
static_assert(sizeof(U1) == 4 && alignof(U1) == 1);
static_assert(sizeof(P2) == 6 && alignof(P2) == 2);
static_assert(sizeof(Box<int>) == 5);
static_assert(sizeof(Natural) == 16);

static long read(const P1 *p) { return p->l + p->s; }

int main()
{
    P1 p[2] = {{1, 40, 2}, {3, 50, 4}};
    Box<double> b{1, 2.5};
    P2 q{1, 7};
    if (read(&p[0]) != 42 || read(&p[1]) != 54)
        return 1;
    if (__builtin_offsetof(P1, s) != 9 || b.t != 2.5 || q.i != 7)
        return 2;
    return 0;
}
