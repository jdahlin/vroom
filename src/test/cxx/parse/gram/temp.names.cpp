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
