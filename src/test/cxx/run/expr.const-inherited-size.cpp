// [expr.const]/5: an inherited member function that reads nothing, called
// on a parameter (SkSpan<T, N>::size())
template <class T, unsigned long E> struct Storage {
    T *p;
    constexpr unsigned long size() const { return E; }
};
template <class T, unsigned long E> struct Span : private Storage<T, E> {
    Span(T *q) : Storage<T, E>{q} {}
    using Storage<T, E>::size;
    T &at(int i) { return this->p[i]; }
};
struct Base {
    int x;
    constexpr int four() const { return 4; }
};
struct Derived : Base {};
int f(Span<int, 3> a, Span<int, 3> b, Derived d)
{
    static_assert(a.size() == b.size());
    static_assert(d.four() == 4);
    int sum = 0;
    for (unsigned long i = 0; i < a.size(); i++)
        sum += a.at(static_cast<int>(i)) * b.at(static_cast<int>(i));
    return sum + d.x;
}
int main()
{
    int v[3] = {1, 2, 3};
    Derived d{{5}};
    return f(Span<int, 3>(v), Span<int, 3>(v), d) != 19;
}
