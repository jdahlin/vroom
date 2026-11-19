/* [over.built]/1: a conversion function to a class type makes no built-in
   candidate viable, so a < 0 picks the friend operator (as <compare>'s
   weak_ordering, which converts to partial_ordering). */
struct partial {
    int v;
};

struct weak {
    int v;
    constexpr operator partial() const { return partial{v}; }
    friend constexpr bool operator<(weak w, int z) { return w.v < z; }
    friend constexpr int sign(weak w)
    {
        return w < 0 ? -1 : (w.v > 0 ? 1 : 0);
    }
};

int main()
{
    weak a{-3}, b{4};
    if (!(a < 0) || b < 0)
        return 1;
    if (sign(a) != -1 || sign(b) != 1 || sign(weak{0}) != 0)
        return 2;
    return 0;
}
