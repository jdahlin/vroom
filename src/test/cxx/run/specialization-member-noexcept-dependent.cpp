// An explicit specialization (libc++'s less<void>) whose member function
// template has a value-dependent noexcept-specifier: a specialization of
// another argument (less<int>, as std::map's default comparator) does not
// match it, the noexcept being no part of its template argument list.
extern "C" int printf(const char *, ...);
template <class T> T &&fwd(T &t) { return static_cast<T &&>(t); }
template <class T = void> struct less {
    bool operator()(const T &a, const T &b) const { return a < b; }
};
template <> struct less<void> {
    template <class A, class B>
    auto operator()(A &&a, B &&b) const noexcept(noexcept(fwd(a) < fwd(b)))
        -> decltype(fwd(a) < fwd(b)) {
        return fwd(a) < fwd(b);
    }
};
template <class K, class C = less<K>> struct map { C c; bool lt(K a, K b) { return c(a, b); } };
int main() {
    map<int> m;
    map<long, less<>> t;
    printf("%d %d\n", m.lt(1, 2), t.lt(3, 2));
    return m.lt(1, 2) + t.lt(3, 2) - 1;
}
