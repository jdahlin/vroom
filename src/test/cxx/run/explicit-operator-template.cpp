/* [temp.explicit]/3, [temp.names]/1: an explicit instantiation whose
   declarator is operator-function-id < template-arguments > */
struct S {
    int v;
};
template <class T> T operator+(T a, int b)
{
    a.v += b;
    return a;
}
template <class T> T operator-(T a, T b)
{
    a.v -= b.v;
    return a;
}
template <class T> struct Box {
    T v;
};
template <class T> Box<T> operator*(const T &a, const Box<T> &b)
{
    return Box<T>{a + b.v};
}
/* overloaded: the written arguments select the template */
template <class T, class U> T operator+(T a, U *)
{
    return a;
}
extern template S operator+<S>(S, int);
template S operator+<S>(S, int);
template S operator-(S, S);
template Box<int> operator*<int>(const int &, const Box<int> &);

int main()
{
    S s = {5};
    S t = s + 3;
    if (t.v != 8)
        return 1;
    S u = t - s;
    if (u.v != 3)
        return 2;
    Box<int> b = {4};
    if ((7 * b).v != 11)
        return 3;
    return 0;
}
