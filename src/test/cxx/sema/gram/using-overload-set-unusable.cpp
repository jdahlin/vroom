// [namespace.udecl]/1, [basic.scope.declarative]: a using-declaration of a
// function template's name in a namespace whose templates of that name
// analysis cannot resolve calls of (an inline namespace nested in another
// declares the name too: lookup finds more than the scope's own
// declarations, [namespace.udir]/2) is well-formed; the templates joined
// stay unusable (a call is refused, not guessed).  Before: unsupported for
// [over] (libc++ <__fwd/subrange.h>, with <tuple> after <__chrono/duration.h>).
template <class T> struct pair { T a; };
namespace std {
inline namespace lit { inline namespace deep { template <class T> int get(T); } }
template <int I, class T> int get(pair<T>&) { return 1; }
template <int I, class T> int get(const pair<T>&) { return 2; }
namespace ranges {
template <int I, class T> constexpr int get(pair<T>&&) { return 3; }
}
using ranges::get;
}
