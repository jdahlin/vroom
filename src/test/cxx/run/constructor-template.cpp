// [temp.mem]/1: constructor templates.  The class has a user-declared
// constructor, so it is no aggregate ([dcl.init.aggr]/1.1) and has no
// implicit default constructor ([class.default.ctor]/1).  A construction
// considers the templates' specializations deduced from its arguments
// ([over.match.ctor], [temp.deduct.call]), a substitution failure in a
// default template-argument none ([temp.deduct]/8); so does an implicit
// conversion to the class ([over.ics.user]: libc++'s
// _CmpUnspecifiedParam in <compare>, whose constructor is consteval:
// analysed as constexpr, which emits a symbol clang does not).
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <class T, class U> struct is_same { static const bool value = false; };
template <class T> struct is_same<T, T> { static const bool value = true; };

struct Zero {
    template <class T, class = typename enable_if<is_same<T, int>::value>::type>
    __attribute__((__visibility__("hidden"))) constexpr Zero(T zero) noexcept
    {
        (void)zero;
    }
    static int answer() { return 42; }
};

struct Ordered {
    int v;
    friend constexpr bool operator<(Ordered a, Zero) { return a.v < 0; }
    friend constexpr bool operator<(Zero, Ordered a) { return a.v > 0; }
};

struct Holder {
    template <class T> explicit constexpr Holder(T *p) : value(*p) {}
    int value;
};

/* two templates, the one whose default template-argument substitutes */
struct Pick {
    template <class T, class = typename enable_if<is_same<T, int>::value>::type>
    Pick(T x) : kind(1), value(x) {}
    template <class T,
              class = typename enable_if<!is_same<T, int>::value>::type,
              class = void>
    Pick(T x) : kind(2), value(static_cast<int>(x)) {}
    int kind, value;
};

int read(const Holder *h) { return h ? h->value : -1; }
int through(Zero *z) { return z ? 1 : 2; }
int kind(Pick p) { return p.kind * 100 + p.value; }

int main()
{
    if (sizeof(Zero) != 1 || sizeof(Holder) != sizeof(int))
        return 1;
    if (Zero::answer() != 42)
        return 2;
    if (read(nullptr) != -1 || through(nullptr) != 2)
        return 3;
    int seven = 7;
    Holder h(&seven);
    if (read(&h) != 7)
        return 4;
    Ordered minus = {-3}, plus = {5};
    bool a = minus < 0;
    bool b = plus < 0;
    bool c = 0 < plus;
    if (!a || b || !c)
        return 5;
    Pick i(3);
    Pick ch('a');
    if (i.kind != 1 || i.value != 3 || ch.kind != 2 || ch.value != 'a')
        return 6;
    int four = kind(4);
    int half = kind(2.5);
    if (four != 104 || half != 202)
        return 7;
    Zero z = 0;
    (void)z;
    return 0;
}
