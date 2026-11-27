/* [expr.new]/10 with [class.dtor]/15: new of one object does not use its
   class's destructor, and a constructor only declared here does not need
   the virtual table ([class.virtual]/17): neither may instantiate the
   members' destructors, which would delete an incomplete type. */
namespace incomplete
{
struct Canvas;
template <class T> struct Del {
    void operator()(T *p) const
    {
        static_assert(sizeof(T) >= 0, "incomplete");
        delete p;
    }
};
template <class T> struct Owner {
    T *p = nullptr;
    Del<T> d;
    ~Owner()
    {
        if (p)
            d(p);
    }
};
struct Base {
    virtual ~Base() {}
};
struct Manager : Base {
    Manager();
    Owner<Canvas> canvas;
};
struct Plain {
    Plain();
    Owner<Canvas> canvas;
};
inline Manager *manager() { return new Manager(); }
inline Plain *plain() { return new Plain; }
}

extern "C" int printf(const char *, ...);
int count;
struct M { ~M() { count++; } };
struct Base { virtual ~Base() { } };
struct A : Base { A() = default; M m; };
struct B : Base { B(); M m; };
B::B() = default;
struct C : Base { C() {} M m; };
struct D : Base { constexpr D() {} M m; };
struct E : Base { M m; };
template<class T> struct F : Base { F(); T m; };
template<class T> F<T>::F() {}
template<class T> struct G : Base { G() {} T m; };
struct H : Base { template<class T> H(T) {} M m; };
struct K : Base { virtual void key(); M m; };
void K::key() {}
struct L : Base { L(int = 0) noexcept; M m; };
inline L::L(int) noexcept {}
struct I { M m; virtual ~I() = default; };
struct J : I { J(); M n; };
J::J() {}
Base *make(int i) {
    switch (i) { case 0: return new A; case 1: return new B; case 2: return new C; case 3: return new D;
    case 4: return new E; case 5: return new F<M>; case 6: return new G<M>; case 7: return new H(1); case 8: return new L; }
    return new A{};
}
int main()
{
    for (int i = 0; i < 9; i++)
        delete make(i);
    I *p = new J;
    delete p;
    printf("%d\n", count);
    return count != 11;
}
