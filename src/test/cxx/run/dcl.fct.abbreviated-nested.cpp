// [dcl.fct]/18: abbreviated member function template in a nested class
// (e.g. Pool::PoolItem::IsValidID(auto index)).

struct Outer {
    struct Inner {
        static inline bool is_positive(auto x) { return x > 0; }
        int add(auto x) const { return base + static_cast<int>(x); }
        int base;
    };
};

int main()
{
    if (!Outer::Inner::is_positive(3) || Outer::Inner::is_positive(-1))
        return 1;
    Outer::Inner in{10};
    if (in.add(5) != 15 || in.add(2.5) != 12)
        return 2;
    return 0;
}
