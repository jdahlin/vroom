// a function template's declared return type with a placeholder, deduced
// per specialization ([dcl.spec.auto]/3, /12), and a trailing-return-type
// naming the function parameters ([basic.scope.param]/1), whose decltype
// of a type-dependent expression the symbol encodes ([abi:mangle.decltype])
struct counter {
    int n;
    int size() const { return n; }
    int add(int a, counter *b) { return a + b->size(); }
};

template <class C> auto length(C &c) -> decltype(c.size()) { return c.size(); }
template <class C> auto added(int k, C *d, C &e) -> decltype(d->add(k, d))
{
    return d->add(k, &e);
}
template <class T> auto next(T x) { return x + 1; }
template <class T> decltype(auto) same(T &x) { return (x); }
template <class T> auto address(T &x) { return &x; }
// used before its definition: the use instantiates it ([dcl.spec.auto]/12)
template <class T> auto twice(T x);
template <class T> auto quad(T x) { return twice(x) + twice(x); }
template <class T> auto twice(T x) { return x * 2; }

int main()
{
    counter c{3};
    int i = 1;
    same(i) = 2;
    *address(i) += 1;
    double d = next(1.5);
    decltype(next('a')) widened = next('a');
    if (length(c) != 3 || added(1, &c, c) != 4 || i != 3 || d != 2.5 ||
        widened != 'b' || sizeof(widened) != sizeof(int) || quad(5) != 20)
        return 1;
    return 0;
}
