// RUN: %clang_cc1 -std=c++11 %s
// RUN: %clang_cc1 -std=c++17 %s
// RUN: %clang_cc1 -std=c++20 %s
// [temp.names]/2: "A name is also considered to refer to a template if it
// is an unqualified-id followed by a < and name lookup finds either one or
// more functions or finds nothing", and is a template-name when "name
// lookup finds ... an overload set that contains a function template".
// A function declared beside a function template of the same name, before
// or after it, leaves the name a template-name; in a complete-class
// context ([class.mem]/6) the members declared later count too.
// Before C++20 this is the only reading that makes these calls; C++20's
// P0846R0 also takes a name that finds only functions as a template-name.
// (Found in libstdc++'s fs_path.h, path::string<C>(), and nlohmann/json's
// binary_reader::parse_ubjson_typed_run<T>(...).)
struct P {
  template <class T> constexpr int s(int n) const { return n + 1; }
  constexpr int s() const;
};
constexpr int P::s() const { return s<char>(1); }
static_assert(P().s() == 2);

struct Q {
  constexpr int r(int n, int) const { return r<char>(n) * 2; }
  template <class T> constexpr int r(int n) const { return n; }
};
int q = Q().r(3, 0);

constexpr int t() { return 0; }
template <class T> constexpr int t(T x) { return x + 1; }
static_assert(t<int>(1) == 2);

namespace N {
constexpr int v() { return 0; }
}
namespace N {
template <class T> constexpr int v(T x) { return x; }
constexpr int w = v<int>(4);
}
static_assert(N::w == 4);

// [temp.names]/3: only a > (or the first > of a >> or >>=) ends a
// template-argument-list; a >= after an expression is the operator
// (libc++'s <tuple>: _BoolConstant<sizeof...(_Tp) >= 1>).
template <bool B> struct GeFlag {};
template <class... T> struct GePack { GeFlag<sizeof...(T) >= 1> f; };
template <class T> struct GeSize { GeFlag<sizeof(T) >= 1> f; };
template <int N> struct GeInt { GeFlag<N >= 1> f; GeFlag<(N >> 1) >= 0> g; };

// [temp.names]/3: a < after a literal is the less-than operator, so it does
// not nest the template-argument-list (libc++'s __mu return type).
template <bool, class T> struct lit_r2 { };
template <class T> struct lit_r2<true, T> { typedef T type; };
template <class T> struct lit_ph { static const int value = 1; };
template <class T>
typename lit_r2<0 < lit_ph<T>::value, T>::type lit_pick(T t) { return t; }

// A non-type template-parameter's default argument ends at '>', and is no
// complete-class context ([class.mem]/6): the member-specification's first
// pass must not skip the rest of the declaration.  (nlohmann/json's
// basic_json::get_impl, enable_if_t<...> = 0.)
template <class B>
struct R {
  B get_impl(B *) const { return B(); }
  template <class V, int = 0> V get_impl(int) const { return V(7); }
  template <class V> V get() const { return get_impl<V>(0); }
};
