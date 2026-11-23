// [temp.mem]/1, [temp.mem.func]/1: a member function template of a class
// that is no template, defined outside the class (libc++'s
// condition_variable::__do_timed_wait): the definition belongs to the
// member template its template-head and function type match, and each
// specialization of the member template is analysed from it.
struct C {
    int base = 10;
    template <class T> int f(T v);
    template <class T> int g(T v) const;
    template <class T> auto h(T v) const -> T;
    template <class T> int k(T v) noexcept;
    template <class T> int d(T v, int extra = 5);
    template <class T> int operator()(T v);
    template <class T> int over(T v);
    int over(int v) { return v + 1000; }
};

template <class T> int C::f(T v) { return base + static_cast<int>(v); }
template <class T> inline int C::g(T v) const { return base * static_cast<int>(v); }
template <class T> auto C::h(T v) const -> T { return v + static_cast<T>(base); }
template <class T> int C::k(T v) noexcept { return static_cast<int>(v) - base; }
template <class T> int C::d(T v, int extra) { return static_cast<int>(v) + extra; }
template <class T> int C::operator()(T v) { return base + 2 * static_cast<int>(v); }
template <class T> int C::over(T v) { return static_cast<int>(v); }

int main()
{
    C c;
    c.base = 7;
    int r = 0;
    if (c.base != 7)
        r |= 1;
    if (c.f(3) != 10 || c.f(2.0) != 9)
        r |= 2;
    if (c.g('a' - 'a' + 2) != 14)
        r |= 4;
    if (c.h(1.5) != 8.5)
        r |= 8;
    if (c.k(10) != 3)
        r |= 32;
    if (c.d(1, 2) != 3)
        r |= 64;
    if (c(1) != 9)
        r |= 128;
    if (c.over(1) != 1001 || c.over(2L) != 2)
        r |= 256;
    return r;
}
