/* [temp.mem]/1, [over.match.funcs]/4: a static member function template called through an object, a pointer to one, with deduced and explicit template arguments, with and without the keyword template ([temp.names]/4); the object expression is evaluated and discarded */
struct S {
    template <class T> static T f(T t) { return t + 1; }
    template <class T, class U = T> static U g(T t) { return U(t) + 2; }
};
template <class X> int through(X &x)
{
    return x.template f<int>(1) + x.f(2) + x.template g<int, long>(3);
}
int counter;
S &next(S &s)
{
    counter++;
    return s;
}
int main()
{
    S s;
    S *p = &s;
    int sum = s.f(1) + s.f<int>(1) + p->f(1) + p->f<long>(1) + s.g<int>(1) + p->g<int, char>(2);
    sum += through(s);
    sum += next(s).f(0);
    return sum - (2 + 2 + 2 + 2 + 3 + 4 + (2 + 3 + 5) + 1) + counter - 1;
}
