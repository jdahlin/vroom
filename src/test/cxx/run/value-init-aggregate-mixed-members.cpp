/* [dcl.init]/8.2: T() of an aggregate whose implicit default constructor is
   non-trivial (a default member initializer, a member with a constructor)
   zero-initializes the object first: the members the constructor leaves
   alone stay zero */
extern "C" int printf(const char *, ...);
extern "C" void *memset(void *, int, unsigned long);
struct counter {
    counter() : total(7) {}
    int total;
};
struct base_data { long first; short second; };
struct mixed {
    bool active = true;
    long untouched;
    int *where;
    counter inner;
    double ratio;
    unsigned n = 3;
};
struct derived : base_data {
    int tag = 9;
    char plain;
};
__attribute__((noinline)) static long scribble(void *p, unsigned long n)
{
    memset(p, 0x5a, n);
    return 1;
}
__attribute__((noinline)) static long check_mixed(long poison)
{
    mixed m = mixed();
    (void)poison;
    return m.active + m.untouched + (long)m.where + m.inner.total + (long)m.ratio + m.n;
}
__attribute__((noinline)) static long check_derived()
{
    derived d = derived();
    return d.first + d.second + d.tag + d.plain;
}
int main()
{
    /* dirty the stack where the temporaries will be */
    char dirty[256];
    scribble(dirty, sizeof dirty);
    long a = check_mixed(scribble(dirty, sizeof dirty));
    scribble(dirty, sizeof dirty);
    long b = check_derived();
    mixed assigned;
    scribble(&assigned, sizeof assigned);
    assigned = mixed();
    long c = assigned.untouched + (long)assigned.where + assigned.inner.total;
    printf("%ld %ld %ld %ld\n", a, b, c, 0L);
    return (int)(a - 12) + (int)(b - 9) + (int)(c - 7);
}
