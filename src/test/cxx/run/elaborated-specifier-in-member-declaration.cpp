/* [basic.lookup.elab]/2: an elaborated-type-specifier in a member
   declaration of a class names the class of that name found by the lookup
   (here ::type, beside the member function type_of and a data member), no
   class is declared in the class's scope ([basic.scope.pdecl]/7.2) */
struct type {
    int k;
};
struct T {
    type t;
    const struct type &record() const
    {
        return t;
    }
    struct type make(int k) const
    {
        struct type r;
        r.k = k;
        return r;
    }
    int type_of(int x) const
    {
        return x + 1;
    }
};

int main()
{
    T x;
    x.t.k = 4;
    if (x.record().k != 4)
        return 1;
    if (x.make(9).k != 9)
        return 2;
    return x.type_of(0) - 1;
}
