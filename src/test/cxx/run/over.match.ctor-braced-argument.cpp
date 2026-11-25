/* [over.match.ctor], [over.ics.list]: braced-init-lists as constructor
   arguments in parentheses, in a declaration, a type conversion and a
   mem-initializer */
struct S {
    int a, b;
};
struct T {
    int v;
    T(const S &s) : v(s.a * 10 + s.b)
    {
    }
    T(const S &s, int k) : v(s.a + s.b + k)
    {
    }
};
struct M {
    S s;
    M(int x) : s({x, x + 1})
    {
    }
};
int main()
{
    int x = 3;
    T t({x, 2});
    T u = T({x, 4});
    T w({1, 2}, 5);
    T y = T({1, 1}, 1);
    M m(7);
    return !(t.v == 32 && u.v == 34 && w.v == 8 && y.v == 3 && m.s.a == 7 &&
             m.s.b == 8);
}
