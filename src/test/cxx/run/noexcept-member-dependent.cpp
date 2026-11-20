// [except.spec]/2, [temp.inst]/16, [class.mem]/6: a noexcept-specifier of a
// member function of a class template, decided for each specialization
template <class T> struct is_nt_default {
    static constexpr bool value = __is_nothrow_constructible(T);
};
struct A {
    A() noexcept {}
    void m() noexcept {}
};
struct B {
    B() {}
    void m() {}
};
template <class T> struct S {
    S() noexcept(__is_nothrow_constructible(T)) : t() {}
    S(const S &) noexcept(is_nt_default<T>::value) : t() {}
    void f(T) noexcept(noexcept(T())) {}
    void g() noexcept(noexcept(T().m())) {}
    void h() noexcept(false) {}
    void k() noexcept(true) {}
    ~S() noexcept(__is_nothrow_constructible(T)) {}
    template <class U> void t2(U) noexcept(noexcept(T())) {}
    T t;
};
template <class T> S<T> &ref() noexcept;
static_assert(noexcept(S<A>()), "");
static_assert(!noexcept(S<B>()), "");
static_assert(noexcept(S<A>(S<A>())), "");
static_assert(!noexcept(S<B>(S<B>())), "");
static_assert(noexcept(ref<A>().f(A())), "");
static_assert(!noexcept(ref<B>().f(B())), "");
static_assert(noexcept(ref<A>().g()), "");
static_assert(!noexcept(ref<B>().g()), "");
static_assert(!noexcept(ref<A>().h()), "");
static_assert(noexcept(ref<A>().k()), "");
static_assert(noexcept(ref<A>().t2(1)), "");
static_assert(!noexcept(ref<B>().t2(1)), "");
template <class T> struct P {
    template <class... U> void v(U...) noexcept((noexcept(U()) && ...));
};
template <class T> struct Q {
    int q;
    void u(T) noexcept(sizeof(T) > 2 && sizeof(q) > 1);
};
struct C {
    void a() noexcept(sizeof(int) == 4);
    void b() noexcept(sizeof(int) != 4);
};
C &rc() noexcept;
static_assert(noexcept(rc().a()), "");
static_assert(!noexcept(rc().b()), "");
int main()
{
    S<A> a;
    S<B> b;
    a.f(A());
    b.f(B());
    return 0;
}
