/* [dcl.fct]/6, [dcl.mptr]/1: a cv-qualifier-seq or ref-qualifier is part of
   the function type a pointer to member function refers to: types that
   differ in it are different types (overloading, deduction, partial
   specialization, mangling [abi:mangle.function-type]), &S::f has the
   type of f's cv-qualifier-seq, and (o.*p)() calls through it */
template <class A, class B> struct same {
    static const bool value = false;
};
template <class A> struct same<A, A> {
    static const bool value = true;
};
struct S {
    int v;
    int plain() { return v; }
    int c() const { return v + 1; }
    int vol(int k) volatile { return v + k; }
    int cv(int a, int b) const volatile { return v + a + b; }
    int nx() const noexcept { return v + 5; }
    int both() const volatile { return v + 7; }
};
static_assert(same<decltype(&S::c), int (S::*)() const>::value);
static_assert(!same<decltype(&S::c), int (S::*)()>::value);
static_assert(!same<decltype(&S::c), int (S::*)() volatile>::value);
static_assert(!same<decltype(&S::c), int (S::*)() const &>::value);
static_assert(!same<int (S::*)() const &, int (S::*)() const &&>::value);
static_assert(same<decltype(&S::vol), int (S::*)(int) volatile>::value);
static_assert(same<decltype(&S::cv), int (S::*)(int, int) const volatile>::value);
static_assert(same<decltype(&S::nx), int (S::*)() const noexcept>::value);
static_assert(!same<decltype(&S::nx), int (S::*)() const>::value);
static_assert(same<decltype(&S::plain), int (S::*)()>::value);

typedef int (S::*ConstGetter)() const;
typedef int Fn(int) volatile;
using Alias = int(int, int) const volatile;
static_assert(same<Fn S::*, int (S::*)(int) volatile>::value);
static_assert(same<Alias S::*, decltype(&S::cv)>::value);

template <class T> struct kind {
    static const int value = 0;
};
template <class R, class C> struct kind<R (C::*)()> {
    static const int value = 1;
};
template <class R, class C> struct kind<R (C::*)() const> {
    static const int value = 2;
};
template <class R, class C, class... A> struct kind<R (C::*)(A...) volatile> {
    static const int value = 3;
};
template <class R, class C, class... A>
struct kind<R (C::*)(A...) const volatile> {
    static const int value = 4;
};
template <class R, class C> struct kind<R (C::*)() const &> {
    static const int value = 5;
};
template <class R, class C> struct kind<R (C::*)() const volatile &&> {
    static const int value = 6;
};
template <class R, class C> struct kind<R (C::*)() const noexcept> {
    static const int value = 7;
};
static_assert(kind<decltype(&S::plain)>::value == 1);
static_assert(kind<decltype(&S::c)>::value == 2);
static_assert(kind<decltype(&S::vol)>::value == 3);
static_assert(kind<decltype(&S::cv)>::value == 4);
static_assert(kind<int (S::*)() const &>::value == 5);
static_assert(kind<int (S::*)() const volatile &&>::value == 6);
static_assert(kind<decltype(&S::nx)>::value == 7);
static_assert(kind<int (S::*)() volatile>::value == 3);

/* overloads that differ only in the function type's qualifiers */
int which(int (S::*)()) { return 1; }
int which(int (S::*)() const) { return 2; }
int which(int (S::*)() volatile) { return 3; }
int which(int (S::*)() const volatile) { return 4; }
int which(int (S::*)() &) { return 5; }
int which(int (S::*)() const &) { return 6; }
int which(int (S::*)() volatile &&) { return 7; }

int call(const S &s, ConstGetter p) { return (s.*p)(); }
static void fail() { __builtin_abort(); }

int main()
{
    S s = {1};
    const S cs = {10};
    volatile S vs = {100};
    int (S::*p)() const = &S::c;
    ConstGetter q = &S::c;
    int (S::*pv)(int) volatile = &S::vol;
    int (S::*pcv)(int, int) const volatile = &S::cv;
    int (S::*pn)() const noexcept = &S::nx;
    S *ps = &s;
    if ((s.*p)() != 2 || (cs.*p)() != 11 || (ps->*q)() != 2 || call(cs, q) != 11)
        fail();
    if ((vs.*pv)(1) != 101 || (vs.*pcv)(1, 2) != 103 || (cs.*pcv)(1, 2) != 13)
        fail();
    if ((s.*pn)() != 6 || (cs.*pn)() != 15)
        fail();
    if (p == nullptr || !(p == q) || p != &S::c)
        fail();
    if (which(&S::plain) != 1 || which(&S::c) != 2 ||
        which(&S::both) != 4 || which((int (S::*)() &)nullptr) != 5 ||
        which((int (S::*)() const &)nullptr) != 6 ||
        which((int (S::*)() volatile &&)nullptr) != 7)
        fail();
    return 0;
}
