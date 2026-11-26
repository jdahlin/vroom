// [stmt.ranged]/1, [dcl.spec.auto]: range-based for over a class template
// specialization whose begin()/end() members have deduced `auto` return types
// (e.g. OpenTTD's `BaseBitSet::begin()` / `end()`).

template <class T> struct BitIter {
    unsigned mask;
    T operator*() const
    {
        unsigned i = 0;
        while (((mask >> i) & 1u) == 0u)
            i++;
        return static_cast<T>(i);
    }
    BitIter &operator++()
    {
        mask &= mask - 1u;
        return *this;
    }
    bool operator!=(const BitIter &o) const { return mask != o.mask; }
};

template <class T> struct BitSet {
    unsigned data;
    auto begin() const { return BitIter<T>{data}; }
    auto end() const { return BitIter<T>{0u}; }
};

int main()
{
    BitSet<int> bs{(1u << 1) | (1u << 3) | (1u << 5)};
    int sum = 0;
    for (int x : bs)
        sum += x;
    if (sum != 9)
        return 1;
    return 0;
}
