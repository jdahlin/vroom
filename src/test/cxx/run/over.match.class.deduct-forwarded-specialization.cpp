/* [over.match.class.deduct]: a lone argument of the template's own
   specialization, an expression in a specialization's body, takes the copy
   deduction candidate (SkAsWritableByteSpan). */
extern "C" int printf(const char *, ...);

template <class T> T &&forward(T &t) { return static_cast<T &&>(t); }

template <class T> struct span {
    T *p;
    int n;
    span(T *p, int n) : p(p), n(n) {}
    span(const span &) = default;
};
template <class T> span(T *, int) -> span<T>;
template <class C> span(C &&) -> span<int>;

template <class T> int bytes(T &&t)
{
    span s(::forward<T>(t));
    return s.n * static_cast<int>(sizeof(*s.p));
}

int main()
{
    char c[3] = {};
    span<char> a(c, 3);
    printf("%d %d %d\n", bytes(a), bytes(span<char>(c, 2)), bytes(a));
    return 0;
}
