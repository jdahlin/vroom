/* [class.copy.ctor]/14: passing an empty class by value reads nothing, so
   a variable that is no constant may be the argument of a constant
   evaluation (Highway's constexpr size_t kN = MaxLanes(d)) */
template <class T, int N> struct Simd {
    constexpr unsigned long Lanes() const { return N; }
};
template <class T, int N> constexpr unsigned long MaxLanes(Simd<T, N> d)
{
    return d.Lanes();
}
struct Tag {
};
constexpr int tagged(Tag) { return 8; }

template <class D> unsigned long f(D d)
{
    constexpr unsigned long kN = MaxLanes(d);
    return kN;
}

int main()
{
    Tag t;
    constexpr int n = tagged(t);
    return f(Simd<int, 4>()) + n != 12;
}
