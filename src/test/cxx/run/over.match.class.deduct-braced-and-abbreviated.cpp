/* [over.match.class.deduct]/1.1: an abbreviated constructor template's
   guide; [temp.deduct.type]/5.6: a braced-init-list argument deduces
   nothing where another argument deduces the parameter */
struct S {
    int a, b;
};
template <typename T> struct Backup {
    Backup(T &o, T nv) : original(o), old(o) { o = nv; }
    ~Backup() { original = old; }
    T &original;
    T old;
};
template <typename It> struct V {
    V(auto &&range, It middle) : first(range), mid(middle) {}
    It first, mid;
};
int main()
{
    S s{1, 2};
    {
        Backup b(s, {.a = 3, .b = 4});
        if (s.a != 3 || s.b != 4)
            return 1;
    }
    {
        Backup b(s, {5, 6});
        if (s.b != 6)
            return 2;
    }
    if (s.a != 1)
        return 3;
    int a[3] = {1, 2, 3};
    V x{a, a + 1};
    V y(a, a + 2);
    return !(*x.mid == 2 && *y.mid == 3 && *x.first == 1);
}
