// One case per rule the json proof of concept needed (PLAN-cxx.md X3); each parses with clang++ -fsyntax-only.
namespace std { inline namespace __1 { template <class T> struct vector { using value_type = T; }; } }
namespace std { using size = int; }                              // a reopened namespace keeps its inline one
std::vector<int>::value_type a1;                                  // through the inline namespace
template <class T> struct B { typedef T type; };
template <class T> struct D : B<T> { using U = B<T>::type; };     // implicit typename in an alias (C++20)
template <class T> struct E : B<T> { using typename B<T>::type; type f(type x); };
struct S { int f(int); int g() { return f(1), 2; } };             // a body sees later members; return a, b
int x1 = int(1.5), x2 = static_cast<int>(2), x3 = sizeof(int) >= 1;
template <bool> struct C {};
C<sizeof(int) >= 1> c1;                                           // >= is an operator in template arguments
struct P { int a; int f() const; }; int (P::*pm)() const = &P::f; // pointer to member
struct Q { Q(int); }; Q::Q(int) {}                                // X::X is a constructor
template <class T> struct G { G(T); }; template <class T> G(T) -> G<T>;   // a deduction guide
int y(int z) { auto [p, q] = (int[2]){z, z}; return p + q; }      // a structured binding
struct It { It(int); }; struct Str { Str(It, It); };
void v(int f) { Str s((It(f)), (It(1))); }                        // not a function declaration
struct J { template <class T> T get() const; }; int w = J().get<int>();   // a member template call
std::vector<::std::size> vv;                                      // <:: is < ::
