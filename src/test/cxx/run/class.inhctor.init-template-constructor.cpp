/* [class.inhctor.init]/1, [namespace.udecl]/14: an inherited constructor
   template initializes the base subobject and the rest of the derived class,
   through every class between them, at run time and in constant
   evaluation */
/* cxx-run-flags: -std=c++20 */
struct other {
    int o = 5;
};
struct tbase {
    template <class A>
    constexpr tbase(int, A &&v) : data(v)
    {
    }
    int data;
};
struct mid : other, tbase {
    using tbase::tbase;
    int extra = 7;
};
struct leaf : mid {
    using mid::mid;
    int last = 9;
};
struct plain {
    constexpr plain(int v) : p(v) {}
    int p;
};
struct from_plain : plain {
    using plain::plain;
    int q = 2;
};
constexpr int run()
{
    leaf d(0, 3);
    from_plain f(4);
    return d.data + d.extra + d.o + d.last + f.p + f.q;
}
static_assert(run() == 30);
static int value = 3;
int main()
{
    mid m(0, value);
    leaf l(0, 4L);
    if (m.data != 3 || m.extra != 7 || m.o != 5)
        return 1;
    if (l.data != 4 || l.last != 9)
        return 2;
    return run() != 30;
}
