// [dcl.spec.auto]/11: deducing a member function's placeholder return type
// on demand in undeduced_use must not hold a reference across entity_table growth.
struct S {
    static auto make() { return 42; }
    int use()
    {
        using T = decltype(make());
        T v = make();
        return v;
    }
};

int main()
{
    S s;
    return s.use() == 42 ? 0 : 1;
}
