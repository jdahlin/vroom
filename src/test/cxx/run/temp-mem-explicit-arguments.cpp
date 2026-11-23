/* [temp.mem]/1, [temp.arg.explicit], [temp.names]/4: a non-static member function template called with explicit template arguments (all, some, defaulted), through an object and a pointer, with and without the keyword template, and through a dependent object */
struct S {
    template <class T> T get() const { return T(5); }
    template <class T, class U = int> T conv(U u) const { return T(u) + 1; }
    template <class T, class U> T both(U u) { return T(u) * 2; }
};
template <class X> int through(X &x, const X &cx)
{
    return x.template get<int>() + cx.template conv<long>(1) + x.template both<int, char>(2);
}
int main()
{
    S s;
    const S c = {};
    S *p = &s;
    int r = s.get<int>() + c.get<char>() + p->get<long>() + s.conv<int>(1) + s.conv<int, char>(2) + p->both<int>(3L);
    r += through(s, c);
    return r - (5 + 5 + 5 + 2 + 3 + 6 + (5 + 2 + 4));
}
