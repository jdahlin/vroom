/* [temp.mem]/1, [class.ctor]: constructor templates defined in the class, of several parameters, explicit, and delegating */
struct S {
    int v;
    template <class T, class U> S(T t, U u) : v(int(t) + int(u)) {}
    template <class T> S(T t) : S(t, 1) {}
    template <class T> explicit S(T *p, int) : S(*p) { v += 100; }
};
int main()
{
    S b(1, 'a');
    int x = 5;
    S c(&x, 0);
    S d(7);
    return b.v - 98 + c.v - 106 + d.v - 8;
}
