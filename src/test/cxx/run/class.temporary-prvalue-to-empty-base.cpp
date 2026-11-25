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
int main()
{
    int a = take(Agg{{}, 1});
    int b = take(make());
    int before = destroyed;
    int c = make().seven();
    return !(a == 7 && b == 7 && before >= 1 && c == 7 && destroyed > before);
}
