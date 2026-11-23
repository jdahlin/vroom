// [class.friend] and [temp.friend]: the standard's examples and their near
// misses, one declaration per rule: bench/cxx-sema.py compares each
// verdict with clang's.

// /1: a friend is not a member, but may use private names
namespace p1 {
class X {
  int a;
  friend void friend_set(X *, int);
public:
  void member_set(int);
};
void friend_set(X *p, int i) { p->a = i; }
void X::member_set(int i) { a = i; }
void not_friend(X *p, int i) { p->a = i; }   // error: a is private
}

// /2: a befriended class's bases, members and nested classes
namespace p2 {
class A {
  class B {};
  enum { a = 100 };
  friend class X;
  friend class Y;
};
struct X : A::B {
  A::B mx;
  class Y { A::B my; };
};
class Y { int v[A::a]; };
class Z { int v[A::a]; };                    // error: A::a is private
class D { friend class E {}; };              // error: defined in a friend
}

// /3: the three forms; a type that is no class is ignored
namespace p3 {
class C;
typedef C Ct;
class X1 { friend C; };
class X2 { friend Ct; };
class X3 { friend class D; };
class X4 { friend int; };
class X5 { friend const C; };                // error: cv-qualifier
class X6 { int friend; };                    // error: friend not first
class X7 { friend unsigned int; };
template <typename T> class R { friend T; int secret; };
class C { int get(R<C> &r) { return r.secret; } };
R<int> ri;
int nc(R<int> &r) { return r.secret; }       // error: int is not a friend
typedef int I;
class X8 { friend class I; };                // error: a typedef-name
union U;
class X9 { friend struct U; };               // error: U is a union
}

// /3: a class nested in the class granting friendship
namespace nested {
class N {
  class Rep {};
  friend class Rep;
  int x;
};
}

// /6, /8: definitions and storage classes
namespace p6 {
void f1();
struct X { void f2(); };
struct Y {
  friend void ::p6::f1() {}                  // error: qualified definition
};
struct Y2 {
  friend void X::f2() {}                     // error: qualified definition
};
struct M { friend void f() {} };
struct S1 { friend static void g(); };       // error: storage class
struct S2 { friend extern void h(); };       // error: storage class
}

// /10: friendship is neither inherited nor transitive
namespace p10 {
class A {
  friend class B;
  int a;
};
class B { friend class C; };
class C {
  void f(A *p) { p->a++; }                   // error: not a friend of A
};
class D : public B {
  void f(A *p) { p->a++; }                   // error: derived from a friend
};
class B2 { void f(A *p) { p->a++; } };
}

// /11: friends of a local class
namespace p11 {
class X;
void a();
void f() {
  class Y;
  extern void b();
  class A {
    friend class X;
    friend class Y;
    friend class Z;
    friend void b();
  };
  X *px;
}
void g() {
  class A { friend class Z; };
  Z *pz;                                     // error: no Z is found
}
void h() { class A { friend void a(); }; }   // error: ::a not considered
void i() { class A { friend void c(); }; }   // error: no prior declaration
void j() {
  void d();
  class A { friend void d() {} };            // error: definition
}
}

// [temp.friend]/2, /3: a friend class template
namespace temp2 {
class X {
  template <class T> friend struct A;
  class Y {};
  int y;
};
template <class T> struct A {
  X::Y ab;
};
A<int> ai;
struct B { int get(X &x) { return x.y; } };  // error: not a friend
}

// [temp.friend]/6, /7
namespace temp6 {
template <class T> class A {};
class X {
  template <class T> friend class A<T *>;    // error: partial specialization
};
template <class U> struct S {};
class Y {
  template <class U> friend S<U>;            // error: no elaborated type
};
void f() {
  struct L {
    template <class T> friend void g(T);     // error: in a local class
  };
}
}

// [temp.friend]/3: friend class templates, qualified or not: every
// specialization is a friend (checked through a dependent member
// declaration, which instantiation checks)
namespace temp3 {
struct C { template <class> struct D; };
namespace N { template <class> struct M; }
class X {
  template <class> friend struct C::D;
  template <class> friend struct N::M;
  template <class> friend struct Later;
  enum { a = 3 };
};
template <class T> struct C::D { int v[T::a]; };
template <class T> struct N::M { int v[T::a]; };
template <class T> struct Later { int v[T::a]; };
C::D<X> d;
N::M<X> m;
Later<X> l;
}

// [temp.friend]/1, /3: a friend class template declared before
namespace temp1 {
template <class T> struct A;
class X {
  template <class> friend struct A;
  enum { a = 3 };
};
template <class T> struct A { int v[T::a]; };
A<X> ax;
}

// [temp.friend]/4: a member of a dependent type
namespace temp4 {
template <class T> struct A { struct B; };
class C {
  template <class T> friend struct A<T>::B;
  enum { a = 1 };
};
template <class T> struct A<T>::B { int v[T::a]; };
template <> struct A<int> { struct B { int v[C::a]; }; };
A<C>::B ab;
template <class T, class U> struct P { struct Q; };
class D {
  template <class T, class U> friend struct P<T, U>::Q;
  enum { a = 1 };
};
template <class T, class U> struct P<T, U>::Q { int v[T::a]; };
P<D, int>::Q pq;
}

// /5: members of another class, special members implicitly declared
namespace p5 {
struct A {};
struct B {
  friend constexpr A::A() noexcept;
  friend constexpr A::A(const A &) noexcept;
  friend constexpr A::A(A &&) noexcept;
  friend constexpr A::~A();
  friend constexpr A &A::operator=(const A &) noexcept;
  friend constexpr A &A::operator=(A &&) noexcept;
};
struct B1 { friend A::A() noexcept; };        // error: not constexpr
struct B2 { friend constexpr A::A(); };       // error: not noexcept
struct U { U(int); ~U(); U &operator=(const U &); };
struct V {
  friend U::U(int);
  friend U::~U();
  friend U &U::operator=(const U &);
};
struct W1 { friend U::U(); };                // error: U has a constructor
struct W2 { friend U &U::operator=(U &&); }; // error: no move assignment
template <class T> struct X { int f(const T &) const; void g(); };
class Y {
  friend int X<Y>::f(const Y &) const;
  friend void X<Y>::g();
  static const int v = 2;
};
struct W3 { friend void X<Y>::g() const; };  // error: g is not const
}

// [temp.friend]/4: a member function of a dependent type
namespace temp4f {
template <class T> struct A;
class C {
  static void foo();
  template <class T> friend void A<T>::f();
  template <class T> friend int A<T>::h(T) const;
};
template <class T> struct A {
  void f() { C::foo(); }
  int h(T) const;
  void g();
};
template <class T> int A<T>::h(T) const { C::foo(); return 0; }
template <> struct A<char> { void f() { C::foo(); } };
template <class T> void A<T>::g() { C::foo(); }    // error: g is no friend
}

// [temp.friend]/4, the standard's example 4: deduction from C (A<T *>
// is in temp4p: clang does not analyse it and turns off access control)
namespace temp4x {
template <class T> struct A {
  struct B {};
  void f();
  struct D { void g(); };
};
template <> struct A<int> { struct B {}; int f(); struct D { void g(); }; };
class C {
  template <class T> friend struct A<T>::B;
  template <class T> friend void A<T>::f();
  enum { a = 1 };
};
int A<int>::f() { return C::a; }               // error: return type differs
class X {
  template <class T> friend void A<T>::D::g();   // error: not C at the end
};
template <class T> struct E;
class G {
  template <class T, class U> friend struct E<T>::F;   // error: U not deducible
};
}

// [temp.friend]/9: constrained friends
namespace temp9 {
template <class T> concept Big = sizeof(T) > 1;
template <class T> struct N {
  friend int f(N) requires Big<T> { return 1; }
  friend int f(N) requires (!Big<T>) { return 2; }
  template <class U> friend int g(U, N) requires Big<T> { return 3; }
};
N<int> ni;
N<char> nc;
int a = f(ni);
int b = f(nc);
static_assert(sizeof(f(ni)) == sizeof(int));
struct P {
  friend void h() requires true;             // error: not a definition
};
struct Q {
  friend void k() requires true {}           // error: not templated
};
template <class R> concept Any = true;
template <class S> struct T1 {
  template <Any U> friend void d();          // a constraint of its own
};
T1<int> t1;
}
