/* [class.nest]: member functions, constructors and static members of
   nested classes (symbols with <nested-name>), their bodies in the
   enclosing class's complete-class context ([class.mem]/6) */
namespace ns {
struct Outer {
    struct Inner {
        int v;
        Inner(int x) : v(x) {}
        int twice() const { return v * 2 + Outer::bias(); }
        int outer_value(const Outer &o) const { return o.later; }
        static int count();
    };
    static int bias() { return 1; }
    int later;
    Inner make() const { return Inner(later); }
};
int Outer::Inner::count() { return 3; }
}

int main()
{
    ns::Outer o;
    o.later = 5;
    ns::Outer::Inner i = o.make();
    int bad = 0;
    if (i.twice() != 11) bad |= 1;
    if (i.outer_value(o) != 5) bad |= 2;
    if (ns::Outer::Inner::count() != 3) bad |= 4;
    return bad;
}
