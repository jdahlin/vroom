/* [temp.deduct.conv]/2-4: a conversion function template's return type P is
   deduced from the required type A with P's reference removed, and, when A
   is not a reference, P's top-level cv-qualifiers ignored; then
   [temp.deduct.partial]/3.2 with /5, /7 and /9 orders two templates by
   their transformed return types: the lvalue reference, and the more
   cv-qualified type, is the more specialized. */
struct Ref {
    template <class T> operator T &()
    {
        static T t{2};
        return t;
    }
    template <class T> operator const T &()
    {
        static T t{3};
        return t;
    }
};
struct Ptr {
    template <class T> operator T *() { return nullptr; }
    template <class T> operator const T *() { return (const T *)&cell; }
    int cell = 4;
};
struct Lvalue {
    template <class T> operator T &()
    {
        static T t{6};
        return t;
    }
    template <class T> operator T &&()
    {
        static T t{7};
        return (T &&)t;
    }
};
struct Plain {
    template <class T> operator T() { return T(1); }
    template <class T> operator T *() { return nullptr; }
};

int main()
{
    int bad = 0;
    Ref r;
    int j = r;
    if (j != 3)
        bad |= 1;
    Ptr p;
    const int *q = p;
    if (q != &p.cell)
        bad |= 2;
    Lvalue l;
    int &k = l;
    if (k != 6)
        bad |= 4;
    Plain a;
    int *z = a;
    int i = a;
    if (z != nullptr || i != 1)
        bad |= 8;
    return bad;
}
