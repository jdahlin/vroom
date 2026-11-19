/* [temp.alias]/1, [temp.mem]/1: a member alias template of a class of
   namespace scope, named in the class, qualified, and through a dependent
   X::template name (libc++'s __iter_concept_concept_test::_Apply) */
template <class T> struct box { using type = T; };
struct test {
    template <class U> using apply = typename box<U>::type;
    int f() { apply<int> x = 4; return x; }
};
template <class X, class U> using call = typename X::template apply<U>;
int main()
{
    test t;
    call<test, int> v = 38;
    test::apply<long> w = 0;
    return t.f() + v + (int)w - 42;
}
