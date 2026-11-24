// std: c++20
// [over.match.class.deduct]/1.1: the implicit deduction guides of a class
// template's constructors, constructor templates too: the guide's template
// parameters are the class template's followed by the constructor
// template's, and a default template argument of the class is used
typedef decltype(sizeof(0)) size_t;
static const size_t dynamic = 1000;
template <class T, size_t Extent = dynamic>
struct span {
    T *ptr;
    size_t count;
    span() : ptr(0), count(0) {}
    template <class Integer>
    span(T *p, Integer n) : ptr(p), count(static_cast<size_t>(n)) {}
    template <size_t N>
    span(T (&a)[N]) : ptr(a), count(N) {}
};
template <class A, class B>
struct pair {
    A first;
    B second;
    pair(const A &a, const B &b) : first(a), second(b) {}
};
template <class T>
struct holder {
    T value;
    template <class U>
    holder(U u, T t) : value(t + static_cast<T>(u)) {}
};
int main()
{
    int data[4] = {1, 2, 3, 4};
    span s(data, 3);
    static_assert(sizeof(s) == sizeof(span<int>), "");
    if (s.count != 3 || s.ptr[2] != 3)
        return 1;
    span t(data, 2L);
    if (t.count != 2)
        return 2;
    span u(data);
    if (u.count != 4)
        return 3;
    pair p(1, 2.5);
    if (p.first != 1 || p.second != 2.5)
        return 4;
    holder h(3L, 4);
    if (h.value != 7)
        return 5;
    return 0;
}
