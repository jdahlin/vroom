/* [temp.mem]/2, [over.match.funcs]/2-4: static and non-static members, templates and not, of one name: one set; a static one has no implicit object parameter */
struct S {
    static int f(int a) { return a; }
    template <class T> T f(T a, T b) { return a + b; }
    template <class T> static T f(T a, T b, T c) { return a + b + c; }
    int f(int a, int b, int c, int d) const { return a * b * c * d; }
    template <class T> static T h(T a) { return a + 1; }
    template <class T> T h(T a, int) const { return a + 2; }
    int h(char) { return 100; }
    static int k(int) { return 1; }
    template <class T> static int k(T *) { return 2; }
    template <class T> int k(T, T) { return 3; }
};
int main()
{
    S s;
    const S c = {};
    int r = s.f(1) + s.f(1, 2) + s.f(1, 2, 3) + S::f(1, 2, 3) + S::f(4) + c.f(1, 1, 2, 3);
    r += s.h(1) + s.h(1, 0) + c.h(1, 0) + s.h('a') + S::h(5);
    int i = 0;
    r += s.k(1) + s.k(&i) + s.k(1, 2) + S::k(&i) + S::k(7);
    return r - (1 + 3 + 6 + 6 + 4 + 6 + (2 + 3 + 3 + 100 + 6) + (1 + 2 + 3 + 2 + 1));
}
