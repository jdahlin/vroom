/* [ext:clang_source_location]: __builtin_LINE, __builtin_COLUMN,
   __builtin_FILE, __builtin_FILE_NAME and __builtin_FUNCTION are the
   place they are written at, and, in a default argument
   ([dcl.fct.default]/5), the call's; in a default member initializer, the
   constructor's.  The values are clang's (the line numbers below). */
static bool same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
static bool ends_with(const char *s, const char *suffix)
{
    unsigned n = 0, m = 0;
    while (s[n])
        n++;
    while (suffix[m])
        m++;
    return n >= m && same(s + n - m, suffix);
}

struct where {
    unsigned line, column;
    const char *file, *function;
};
where here(unsigned line = __builtin_LINE(), unsigned column = __builtin_COLUMN(),
           const char *file = __builtin_FILE(),
           const char *function = __builtin_FUNCTION())
{
    return {line, column, file, function};
}
int narrowed(int line = __builtin_LINE()) { return line; }
template <class T> unsigned templated(T, unsigned line = __builtin_LINE())
{
    return line;
}
struct member {
    unsigned line = __builtin_LINE();
    const char *function = __builtin_FUNCTION();
    member() {}
    member(int) {}
};
struct with_default {
    unsigned line;
    with_default(unsigned l = __builtin_LINE()) : line(l) {}
};
struct holder {
    unsigned call(unsigned l = __builtin_LINE()) { return l; }
};

const char *in_destructor;
struct scoped {
    ~scoped() { in_destructor = here().function; }
};
struct arithmetic {
    const char *function;
    arithmetic operator+(arithmetic) const { return {here().function}; }
};

int main()
{
    int bad = 0;
    where a = here();
    if (a.line != 65 || a.column != 15 || !same(a.function, "main") ||
        !ends_with(a.file, "builtin-source-location-values.cpp"))
        bad |= 1;
    where b = here(1, 2, "x", "y");
    if (b.line != 1 || b.column != 2 || !same(b.file, "x") ||
        !same(b.function, "y"))
        bad |= 2;
    if (narrowed() != 73 || narrowed(3) != 3)
        bad |= 4;
    if (templated(1) != 75)
        bad |= 8;
    unsigned line = __builtin_LINE();
    unsigned column = __builtin_COLUMN();
    if (line != 77 || column != 23)
        bad |= 16;
    static_assert(__builtin_LINE() == 81, "");
    constexpr unsigned q = __builtin_LINE();
    static_assert(q == 82, "");
    const char *base = __builtin_FILE_NAME();
    if (!same(base, "builtin-source-location-values.cpp"))
        bad |= 32;
    member m, n(1);
    if (m.line != 42 || !same(m.function, "member") || n.line != 43)
        bad |= 64;
    with_default w;
    if (w.line != 90)
        bad |= 128;
    holder h;
    if (h.call() != 94)
        bad |= 256;
    {
        scoped s;
    }
    if (!same(in_destructor, "~scoped"))
        bad |= 256;
    arithmetic x{}, y{};
    if (!same((x + y).function, "operator+"))
        bad |= 512;
    return bad;
}
