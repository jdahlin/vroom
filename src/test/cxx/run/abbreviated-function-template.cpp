/* [dcl.fct]/18-22: abbreviated function templates: a generic parameter
   type placeholder (auto, C auto) invents a type template-parameter,
   appended after the declared ones; a deleted one is a candidate like any
   other, chosen only where it is the better match */
template <class T> concept four = sizeof(T) == 4;
int add(auto x, const auto &y) { return x + y; }
long twice(auto *p) { return *p * 2; }
template <class R> R convert(auto x) { return static_cast<R>(x) + 1; }
int only_four(four auto x) { return x; }
long only_four(auto x) { return x * 10; }
int forward(auto &&x) { return x; }
struct S {
    int k = 3;
    int get(auto x) const { return k + x; }
    template <class T>
        requires four<T>
    int operator()(T &&x) const { return x; }
    void operator()(auto &&) const = delete;
    int scale(four auto x, auto y) const { return x * y; }
};
int main()
{
    int i = 2;
    long l = 5;
    S s;
    if (add(1, 2) != 3 || add(1, 2L) != 3 || twice(&l) != 10)
        return 1;
    if (convert<long>(2.5) != 3 || only_four(7) != 7 || only_four('a') != 970)
        return 2;
    if (forward(i) != 2 || forward(4) != 4)
        return 3;
    if (s.get(4) != 7 || s.get('\1') != 4 || s(9) != 9 || s.scale(2, 3) != 6)
        return 4;
    return 0;
}
