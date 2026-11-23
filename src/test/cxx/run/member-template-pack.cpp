/* [temp.variadic]/1, [temp.param]/14: a member function template or a
   constructor template with a template parameter pack, named or not */
struct S {
    template <class...> static constexpr bool unnamed() { return false; }
    template <class... A> int count(A... a) { return sizeof...(A); }
    template <class T, class... A> int first_plus(T t, A &&...a)
    {
        return t + sizeof...(A);
    }
    template <class... A> S(A... a) { n = sizeof...(A); }
    int n;
};
int main()
{
    S s(1, 2);
    S e;
    int r = s.count(1, 2, 3) + s.first_plus(4, 1, 2) + s.n + e.n;
    if (r != 3 + 6 + 2 + 0)
        return 1;
    return 0;
}
