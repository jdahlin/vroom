// [dcl.fct.def.general]/8: __PRETTY_FUNCTION__ of a member of a class
// template with an unnamed template-parameter; [impl.clang] clang lists
// only the named parameters ([T = char]).
static const char *seen;
template <typename, typename T> struct S {
    static void f() { seen = __PRETTY_FUNCTION__; }
};
template <typename, int> void g() { seen = __PRETTY_FUNCTION__; }
static bool same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
int main()
{
    S<int, char>::f();
    if (!same(seen, "static void S<int, char>::f() [T = char]"))
        return 1;
    g<int, 3>();
    if (!same(seen, "void g()"))
        return 2;
    return 0;
}
