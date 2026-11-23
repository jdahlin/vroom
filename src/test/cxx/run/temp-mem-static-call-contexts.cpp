/* [temp.mem]/2, [class.static.mfct]/2: a static member function template
   called by its unqualified name where there is no object of its class: a
   nested class's member functions, a static data member's initializer, a
   lambda in a static member function; and through (*this) in a default
   member initializer. */
struct O {
    template <class T> static int f(T t) { return t + 1; }
    template <class T> int g(T t) { return t + 2; }
    struct I {
        int g() { return f(1); }
        static int h() { return f(2) + O::f(3); }
    };
    static constexpr int z = sizeof(f(1)) + sizeof(O::template f<char>('a'));
    int m = g(5) + f(1);
    static int lambda()
    {
        auto l = [](int x) { return f(x); };
        return l(4);
    }
};
int main()
{
    O::I i;
    O o;
    int r = i.g() + O::I::h() + O::z + o.m + O::lambda();
    return r - (2 + 3 + 4 + 8 + 9 + 5);
}
