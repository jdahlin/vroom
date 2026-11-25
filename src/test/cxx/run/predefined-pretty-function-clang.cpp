// [ext:clang_predefined_names] __PRETTY_FUNCTION__ as clang spells it: the declaration's return and
// parameter types as written (typedef-names, an injected-class-name as C<T>, const first, an array
// parameter as a pointer, no names or default arguments), the scope's specializations canonical, a
// lambda's enclosing function with its parameters, the enclosing specializations' arguments after;
// in those arguments a class's defaulted trailing arguments left out (but an explicit
// specialization's), a redundant inline namespace left out, std::nullptr_t, and a preferred_name
// typedef for an instantiated specialization (libc++'s std::string in nlohmann/json's JSON_ASSERT)
extern "C" int strcmp(const char *, const char *);
extern "C" int puts(const char *);
namespace q {
inline namespace v1 {
template <class T> struct al {};
template <class T, class A = al<T>> struct vec {};
template <class T = void> struct less {};
template <> struct less<void> {};
namespace d {
struct x {};
}
} // namespace v1
template <class C> struct bs;
using str = bs<char>;
template <class C> struct [[clang::preferred_name(str)]] bs {};
template <class C> struct bs2;
using str2 = bs2<char>;
template <class C> struct [[clang::preferred_name(str2)]] bs2;
template <class C> struct bs2 {};
template <class C> struct bs5;
using str5 = bs5<char>;
template <class C> struct [[clang::preferred_name(str5)]] bs5;
template <class C> struct bs5 {
  C c;
};
template <class T> struct box {
  T v;
};
using ibox = box<int>;
typedef unsigned long size_type;
template <class B, class I> struct lexer {
  using token_type = int;
  using string_t = box<char>;
  const char *last = nullptr;
  token_type scan(const string_t &, size_type) { return (last = __PRETTY_FUNCTION__), 0; }
  lexer &self() { return (last = __PRETTY_FUNCTION__), *this; }
  token_type const *ptr() const { return (const token_type *)__PRETTY_FUNCTION__; }
  bool key(string_t &) {
    auto l = [&](int q) { return (last = __PRETTY_FUNCTION__), q; };
    return l(1);
  }
};
struct S {
  const char *ctor;
  S(int) : ctor(__PRETTY_FUNCTION__) {}
  static const char *st(box<box<int> >, unsigned long long) noexcept { return __PRETTY_FUNCTION__; }
  const char *c() const & { return __PRETTY_FUNCTION__; }
  const char *v(int (*)(int), int (&)[3], char *const, int = 3) { return __PRETTY_FUNCTION__; }
  const char *arr(const int a[], ibox *) { return __PRETTY_FUNCTION__; }
};
template <class T, int N, class... R> const char *tr(T (&)[N], R...) { return __PRETTY_FUNCTION__; }
} // namespace q
template <class T> struct W {
  static const char *f() { return __PRETTY_FUNCTION__; }
};
int check(const char *got, const char *want) {
  if (strcmp(got, want)) {
    puts(got);
    return 1;
  }
  return 0;
}
int main() {
  int bad = 0;
  q::lexer<q::box<int>, const char *> l;
  q::box<char> s{};
  l.scan(s, 1);
  bad += check(l.last, 
                   "token_type q::lexer<q::box<int>, const char *>::scan(const string_t &, size_type) [B = q::box<int>, I = const char *]");
  l.self();
  bad += check(l.last, 
                   "lexer<B, I> &q::lexer<q::box<int>, const char *>::self() [B = q::box<int>, I = const char *]");
  bad += check((const char *)l.ptr(), 
                   "const token_type *q::lexer<q::box<int>, const char *>::ptr() const [B = q::box<int>, I = const char *]");
  l.key(s);
  bad += check(l.last, 
                   "auto q::lexer<q::box<int>, const char *>::key(string_t &)::(lambda)::operator()(int) const [B = q::box<int>, I = const char *]");
  q::S x(1);
  int ar[3];
  bad += check(x.ctor, 
                   "q::S::S(int)");
  bad += check(q::S::st({}, 1), 
                   "static const char *q::S::st(box<box<int>>, unsigned long long)");
  bad += check(x.c(), 
                   "const char *q::S::c() const &");
  bad += check(x.v(nullptr, ar, nullptr), 
                   "const char *q::S::v(int (*)(int), int (&)[3], char *const, int)");
  bad += check(x.arr(ar, nullptr), 
                   "const char *q::S::arr(const int *, ibox *)");
  bad += check(q::tr(ar, 'a', 2L), 
                   "const char *q::tr(T (&)[N], R...) [T = int, N = 3, R = <char, long>]");
  bad += check(W<q::vec<int>>::f(), 
                   "static const char *W<q::vec<int>>::f() [T = q::vec<int>]");
  bad += check(W<q::vec<int, int>>::f(), 
                   "static const char *W<q::vec<int, int>>::f() [T = q::vec<int, int>]");
  bad += check(W<q::d::x>::f(), 
                   "static const char *W<q::d::x>::f() [T = q::d::x]");
  bad += check(W<q::less<>>::f(), 
                   "static const char *W<q::less<void>>::f() [T = q::less<void>]");
  bad += check(W<q::str>::f(), 
                   "static const char *W<q::bs<char>>::f() [T = q::bs<char>]");
  bad += check(W<decltype(nullptr)>::f(), 
                   "static const char *W<std::nullptr_t>::f() [T = std::nullptr_t]");
  bad += check(W<q::str2>::f(), 
                   "static const char *W<q::bs2<char>>::f() [T = q::bs2<char>]");
  bad += check(W<q::bs2<int>>::f(), 
                   "static const char *W<q::bs2<int>>::f() [T = q::bs2<int>]");
  q::str5 made{};
  (void)made;
  bad += check(W<q::str5>::f(), 
                   "static const char *W<q::str5>::f() [T = q::str5]");
  return bad;
}
