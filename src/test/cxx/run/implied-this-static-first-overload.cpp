/* [over.call.func]/3: an unqualified call in a member function of an
   overload set whose first declaration is static: (*this) for a non-static
   one chosen */
extern "C" int printf(const char *, ...);
template <class C> struct traits {
    int base = 100;
    static int value_of(unsigned char c, int r) { return c + r; }
    int value_of(char c, int r) const { return base + value_of((unsigned char)c, r); }
    int value(C c, int r) const { return value_of(c, r); }
    int pick(char) { return 1; }
    static int pick(int) { return 2; }
    int pick(char) const { return 3; }
    int run(char c) { return pick(c) * 10 + pick(7); }
    int crun(char c) const { return pick(c) * 10 + pick(7); }
};
struct derived : traits<char> {
    int go() { return value_of('a', 1) + value_of((unsigned char)'a', 1); }
};
int main()
{
    traits<char> t;
    derived d;
    printf("%d %d %d %d\n", t.value('a', 2), t.run('x'), t.crun('x'), d.go());
}
