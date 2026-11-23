/* [temp.mem]/1, [class.copy.ctor]/5: a constructor template is never a copy constructor: copying a class with one uses the implicit (or declared) copy constructor */
struct S {
    int v;
    template <class T> S(T t) : v(int(t)) {}
    S(const S &o) : v(o.v + 1000) {}
};
int main()
{
    S a(3);
    S d = a;
    S e(d);
    return a.v - 3 + d.v - 1003 + e.v - 2003;
}
