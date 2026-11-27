/* cxx-run-flags: -std=c++20 */
/* [over.match.copy]/(1.2): T(x) copies what an explicit conversion function
   of x returns by reference; T an aggregate with and
   without default member initializers */
struct plain {
    int l, t;
};
struct initialized {
    int l = 0, t = 0;
};
template <class T> struct wrap {
    T d;
    explicit operator const T &() const { return d; }
};
struct implicit_wrap {
    initialized d;
    operator initialized &() { return d; }
};
template <class T> int value(const T &r) { return r.l + r.t * 10; }

int main()
{
    wrap<plain> a{{1, 2}};
    wrap<initialized> b{{3, 4}};
    implicit_wrap d{{7, 8}};
    if (value(plain(a)) != 21)
        return 1;
    if (value(initialized(b)) != 43)
        return 2;
    if (value(static_cast<initialized>(b)) != 43)
        return 3;
    initialized e = initialized(d);
    d.d.l = 0;
    if (value(e) != 87)
        return 4;
    return 0;
}
