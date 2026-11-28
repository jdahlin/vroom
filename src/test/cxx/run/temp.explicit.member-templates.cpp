/* [temp.explicit]/3: explicit instantiations naming a function template by
   a namespace-qualified declarator-id, a constructor template and a member
   template of a class, and a member template of a class template's
   specialization (fmt's format.cc). */
namespace det {
namespace drag {
template <class T> struct fp { T v; };
template <class T> fp<T> to_decimal(T x) noexcept { return {x + 1}; }
}
struct loc { int v; };
struct locale_ref {
    int got = 0;
    locale_ref() = default;
    template <class L> locale_ref(const L &l);
    template <class L> L get() const;
};
template <class L> locale_ref::locale_ref(const L &l) { got = l.v; }
template <class L> L locale_ref::get() const { return L{got}; }
template <class T> struct buffer {
    int n = 0;
    template <class U> void append(const U *b, const U *e);
};
template <class T> template <class U>
void buffer<T>::append(const U *b, const U *e) { n += int(e - b); }

template drag::fp<float> drag::to_decimal(float x) noexcept;
template auto drag::to_decimal(double x) noexcept -> drag::fp<double>;
template locale_ref::locale_ref(const loc &l);
template auto locale_ref::get<loc>() const -> loc;
template void buffer<char>::append(const char *, const char *);
template void buffer<wchar_t>::append(const wchar_t *, const wchar_t *);
}

int main()
{
    det::loc l = {5};
    det::locale_ref r(l);
    if (r.get<det::loc>().v != 5)
        return 1;
    if (det::drag::to_decimal(1.0f).v != 2.0f ||
        det::drag::to_decimal(2.0).v != 3.0)
        return 2;
    det::buffer<char> b;
    const char *s = "abc";
    b.append(s, s + 3);
    det::buffer<wchar_t> w;
    const wchar_t *t = L"ab";
    w.append(t, t + 2);
    return b.n + w.n - 5;
}
