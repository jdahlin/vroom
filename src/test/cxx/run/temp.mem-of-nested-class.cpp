// [temp.mem]/1: a member template of a nested class defined outside the
// enclosing classes.
namespace n {
struct A {
    struct B {
        int base;
        template <int N> int f(int x) const;
        template <class T> static T g(T);
        struct C {
            template <class T> int h(T);
        };
    };
};
template <int N> int A::B::f(int x) const { return base + x + N; }
template <class T> T A::B::g(T t) { return t + 1; }
template <class T> int A::B::C::h(T t) { return static_cast<int>(t) * 2; }
} // namespace n

int main()
{
    n::A::B b{1};
    n::A::B::C c;
    if (b.f<3>(2) != 6)
        return 1;
    if (n::A::B::g(1) != 2 || n::A::B::g(2L) != 3)
        return 2;
    if (c.h(21.0) != 42)
        return 3;
    return 0;
}
