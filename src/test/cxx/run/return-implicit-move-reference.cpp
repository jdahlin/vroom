/* cxx-run-flags: -std=c++23 */
// [class.copy.elision]/3 (C++23, P2266R3): returning the name of an
// implicitly movable entity is an xvalue, also from a function that returns
// a reference: an rvalue reference to a parameter that is an rvalue
// reference is returned as it is.
struct S {
    int v;
};

int &&pass(int &&n)
{
    return n;
}

S &&pass_class(S &&s)
{
    return s;
}

const S &keep(const S &s)
{
    return s;
}

int main()
{
    int x = 5;
    S s{3};
    int &&r = pass(static_cast<int &&>(x));
    S &&q = pass_class(static_cast<S &&>(s));
    const S &k = keep(s);
    if (r != 5)
        return 1;
    if (q.v != 3 || k.v != 3)
        return 2;
    return 0;
}
