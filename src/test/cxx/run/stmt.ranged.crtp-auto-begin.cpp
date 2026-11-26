// [stmt.ranged]/1, [dcl.spec.auto]: range-based for over a CRTP-derived class
// whose base class template has postponed `auto begin() const` / `auto end() const`
// (e.g. OpenTTD's `EnumBitSet : BaseBitSet<EnumBitSet<...>, ...>`).

template <class T> struct Iter {
    int x;
    T operator*() const { return x; }
    Iter &operator++()
    {
        x++;
        return *this;
    }
    bool operator!=(const Iter &o) const { return x != o.x; }
};

template <class D, class T> struct Base {
    auto begin() const { return Iter<T>{1}; }
    auto end() const { return Iter<T>{5}; }
};

template <class T> struct Derived : Base<Derived<T>, T> {};

int main()
{
    Derived<int> d{};
    int sum = 0;
    for (int x : d)
        sum += x;
    if (sum != 10)
        return 1;
    return 0;
}
