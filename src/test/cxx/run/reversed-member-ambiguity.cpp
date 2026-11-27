/* [over.match.oper], [temp.deduct]/8: a reversed member operator== tied
   with a non-member template makes the expression ambiguous, which
   removes a candidate whose constraint holds it (libc++'s optional == a
   class derived from optional) */
extern "C" int printf(const char *, ...);
struct B {
    int v;
};
template <int N = 0> bool operator==(const B &a, const B &b)
{
    printf("template ");
    return a.v == b.v;
}
struct D : B {
    D(int x) : B{x} {}
    D(const B &b) : B{b.v} {}
    bool operator==(const D &o) const
    {
        printf("member ");
        return v == o.v;
    }
};
template <class T, class = decltype(*(T *)0 == *(D *)0)> int pick(int)
{
    return 1;
}
template <class T> int pick(long) { return 2; }
int main()
{
    D d(1);
    printf("%d %d\n", pick<B>(0), pick<D>(0));
    printf("%d\n", d == d);
}
