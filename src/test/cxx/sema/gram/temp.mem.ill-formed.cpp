// Member templates that are ill-formed, one declaration per rule, with near
// misses that are well-formed ([temp.mem]/1-3, [class.access]/4,
// [class.dtor]/1): bench/cxx-sema.py compares each verdict with clang's.
// A local class of non-closure type shall not have member templates (/2).
void local_class()
{
    struct ok_local { void f() {} };
    struct bad_function { template <class T> void f(); };
    struct bad_class { template <class T> struct In {}; };
    struct bad_variable { template <class T> static T v; };
    auto closure = [](auto x) { return x; };
}
// A member function template shall not be virtual (/3).
struct V1 {
    template <class T> virtual void g(T);
    virtual void f();
};
template <class T> struct V2 {
    template <class C> virtual void g(C);
    virtual void f();
};
// A destructor shall not be a member template (/2).
struct D1 {
    template <class T> ~D1();
};
// Access control rules apply to member template names (/2).
class Access {
    template <class T> static int priv(T) { return 1; }
public:
    template <class T> static int pub(T) { return 2; }
};
int use_public = Access::pub(1);
int use_private = Access::priv(1);
// A member template of a class template defined outside it takes the
// class's template-head followed by the member's (/1).
template <class T> struct string {
    template <class T2> int compare(const T2 &);
    template <class T2> int other(const T2 &);
};
template <class T> template <class T2> int string<T>::compare(const T2 &) { return 0; }
template <class T> int string<T>::other(const T &) { return 0; }
// The equivalent template-head with constraints (/1, Example 2).
template <typename T> concept C1 = true;
template <typename T> concept C2 = sizeof(T) <= 4;
template <C1 T> struct S {
    template <C2 U> void f(U);
    template <C2 U> void g(U);
};
template <C1 T> template <C2 U> void S<T>::f(U) {}
template <C1 T> template <typename U> void S<T>::g(U) {}
// A non-static member function template is called through an object; a
// static one by its class's name too ([expr.prim.id]/2, [temp.mem]/2).
struct Holder {
    template <class T> int nonstatic(T) { return 0; }
    template <class T> static int fine(T) { return 0; }
};
int without_object = Holder::nonstatic(1);
int static_without_object = Holder::fine(1);
