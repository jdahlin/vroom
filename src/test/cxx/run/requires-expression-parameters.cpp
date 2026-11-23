// [expr.prim.req]: a requires-expression with a requirement-parameter-list and simple, type, compound (noexcept,
// return-type-requirement) and nested requirements; a constraint that fails removes the candidate
// std: c++20
template <class T, class U> concept same_as = __is_same(T, U);
template <class T> concept convertible_to_int = __is_convertible(T, int);

template <class T> concept has_less = requires(const T &a, const T &b) { { a < b } -> same_as<bool>; };
template <class T> concept has_size = requires(const T &a) { typename T::size_type; a.size(); };
template <class T> concept has_f = requires(T t) { t.f(); };
template <class T> concept nothrow_f = requires(T t) { { t.f() } noexcept; };
template <class T> concept f_int = requires(T t) { { t.f() } -> convertible_to_int; };
template <class T> concept f_same_int = requires(T t) { { t.f() } -> same_as<int>; };
template <class T> concept wide = requires(T t) { requires sizeof(T) > 1; t.f(); };
template <class T, class U> concept addable = requires(T a, U b) { a + b; };
template <class T> concept deref_ref = requires(T p) { { *p } -> same_as<int &>; };

struct A { void f() const; typedef int size_type; int size() const; };
struct B { int f() noexcept; };
struct C { double f(); };
struct D { bool operator<(const D &) const; };
struct E { };
struct W { char c[4]; int f(); };
struct N { char c; int f(); };

static_assert(has_less<int> && has_less<D> && !has_less<E> && !has_less<int *&> == false);
static_assert(has_size<A> && !has_size<B> && !has_size<int>);
static_assert(has_f<A> && has_f<B> && !has_f<E> && !has_f<int>);
static_assert(!nothrow_f<A> && nothrow_f<B> && !nothrow_f<E>);
static_assert(f_int<B> && f_int<C> && !f_int<A> && !f_int<E>);
static_assert(f_same_int<B> && !f_same_int<C>);
static_assert(wide<W> && !wide<N> && !wide<E>);
static_assert(addable<int, double> && !addable<E, int> && !addable<int *, int *>);
static_assert(deref_ref<int *> && !deref_ref<int> && !deref_ref<const int *>);

template <class T> int pick(T) requires has_f<T> { return 1; }
template <class T> int pick(T) requires (!has_f<T>) { return 2; }
template <class T> concept sized_f = requires(T t) { { t.f() } -> same_as<int>; };
template <sized_f T> int which(T) { return 10; }
template <class T> int which(T) { return 20; }

int main()
{
    A a;
    B b;
    C c;
    E e;
    if (pick(a) != 1 || pick(e) != 2 || pick(1) != 2)
        return 1;
    if (which(b) != 10 || which(c) != 20 || which(e) != 20)
        return 2;
    return 0;
}
