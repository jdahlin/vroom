/* [class.base.init]/2, /13: a class template's constructor initializes a
   base, then the members in declaration order, one member after an
   anonymous union that the constructor assigns */
struct B {
    int x;
    B(int v) : x(v) {}
};
template <class T> struct D : B {
    T m;
    int n = 5;
    D() : B(7), m(3) {}
    int get() { return x + m + n; }
};
template <class T> struct U {
    union {
        T a;
        int b;
    };
    int c;
    U() : c(4) { a = 1; }
    int get() { return a + c; }
};
int main()
{
    D<int> d;
    U<int> u;
    return d.get() + u.get() == 20 ? 0 : 1;
}
