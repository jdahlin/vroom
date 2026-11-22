// [class.union]/2: one variant member of a union may have a default member
// initializer; a default-initialized union has that member active.
extern "C" int printf(const char *, ...);

union U {
    int a;
    long b = 5;
    float c;
};
struct S {
    U u;
    int k = 1;
};
union V {
    int a = 3;
    char c;
};
struct W {
    union Inner {
        short s = 9;
        char t;
    } in;
    int tail = 2;
};

int main()
{
    U x;
    U z{7};
    S s;
    V v;
    W w;
    U *p = new U;
    int r = (int)p->b;
    delete p;
    printf("%ld %d %ld %d %d %d %d\n", x.b, z.a, s.u.b, v.a, w.in.s, w.tail,
           r);
    return 0;
}
