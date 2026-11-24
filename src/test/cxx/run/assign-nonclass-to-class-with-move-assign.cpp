// [class.copy.assign]/2: a class that declares a move assignment operator
// and no copy assignment operator has its implicit copy assignment
// operator defined as deleted; it is a candidate but not viable for a
// right operand that does not convert to const X& ([over.match.viable]),
// so `x = nullptr` calls operator=(decltype(nullptr)).
struct V {
    int *p = nullptr;
    int cleared = 0;
    V() = default;
    V(V &&) = delete;
    V &operator=(V &&o)
    {
        p = o.p;
        return *this;
    }
    V &operator=(decltype(nullptr))
    {
        p = nullptr;
        cleared++;
        return *this;
    }
};
int main()
{
    int x = 1;
    V v;
    v.p = &x;
    v = nullptr;
    if (v.p != nullptr)
        return 1;
    if (v.cleared != 1)
        return 2;
    return 0;
}
