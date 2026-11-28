/* [expr.static.cast]/4: a static_cast to a reference binds a temporary of
   the converted value; to an array reference it may add const, not drop
   it */
struct S {
    int n;
    S(const char *p)
        : n(p[0])
    {
    }
};
template <class To, class From>
concept castable = requires(From &f) { static_cast<To>(f); };
static_assert(castable<const char (&)[2], char[2]>);
static_assert(!castable<char (&)[2], const char[2]>);
int main()
{
    char a[2] = {7, 0};
    const char(&r)[2] = static_cast<const char(&)[2]>(a);
    return static_cast<const S &>(a).n + static_cast<S &&>(a).n + r[0] - 21;
}
