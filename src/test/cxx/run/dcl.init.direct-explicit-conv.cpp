// [dcl.init]/17.6.2, CWG 2327: direct-initialization (including functional
// cast) of a class from another class with an explicit conversion function.

struct SV {
    const char *p;
    constexpr SV(const char *s) : p(s) {}
};

struct Base {};

int main()
{
    int x = [] {
        struct S : Base {
            constexpr explicit operator SV() const { return SV("hi"); }
        };
        SV v = SV(S());
        return v.p[0];
    }();
    if (x != 'h')
        return 1;
    return 0;
}
