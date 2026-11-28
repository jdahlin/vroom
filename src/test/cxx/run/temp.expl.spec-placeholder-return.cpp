/* [temp.expl.spec]/1, [dcl.spec.auto]: explicit specializations of a function
   template with a deduced return type, each deducing its own */
struct A {
    int v;
};
struct B {
    long w;
};
static A a{1};
static B b{2};
template <class T> auto *get_spec(int k);
template <> auto *get_spec<A>(int k)
{
    A *none = nullptr;
    if (k)
        return &a;
    return none;
}
template <> auto *get_spec<B>(int) { return &b; }
int main()
{
    return get_spec<A>(1)->v + get_spec<B>(1)->w != 3 || get_spec<A>(0);
}
