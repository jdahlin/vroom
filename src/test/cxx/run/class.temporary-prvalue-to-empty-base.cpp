/* [class.temporary]/4, [conv.rval]: a class prvalue converted to an empty
   base is materialized and destroyed with its full-expression */
int destroyed;
struct Empty {
    int seven() const
    {
        return 7;
    }
};
struct Agg : Empty {
    int v;
};
struct Dtor : Empty {
    int v = 1;
    ~Dtor()
    {
        destroyed++;
    }
};
Dtor make()
{
    return Dtor();
}
int take(const Empty &e)
{
    return e.seven() + destroyed;
}
/* an empty derived class passed by value as its empty base (a priority
   tag) */
template <int N> struct tag : tag<N - 1> {};
template <> struct tag<0> {};
struct W {};
int tagged(tag<0>, W, const char *a)
{
    return a[0];
}
template <class T> int call(W w, T &&a)
{
    return tagged(tag<1>(), w, a);
}
int main()
{
    if (call(W(), "x") != 'x')
        return 2;
    int a = take(Agg{{}, 1});
    int b = take(make());
    int before = destroyed;
    int c = make().seven();
    return !(a == 7 && b == 7 && before >= 1 && c == 7 && destroyed > before);
}
