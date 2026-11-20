/* [expr.sizeof] and [expr.alignof] where a constant expression is needed
   ([expr.const]): array bounds, template arguments (type and non-type),
   default template arguments, noexcept-specifiers, enumerators,
   alignas of a class, decltype operands, constexpr functions; sizeof...
   of non-type packs and in a member template of a class template pack */
template <int... N> constexpr unsigned long nt() { return sizeof...(N); }
static_assert(nt<>() == 0 && nt<1,2,3>() == 3);
template <class... A> struct Holder { template <class... B> static constexpr unsigned long both() { return sizeof...(A) * 10 + sizeof...(B); } };
static_assert(Holder<int, int>::both<char>() == 21);
template <class T> constexpr unsigned long a1() { return alignof(T) + alignof(T[3]) + alignof(T &); }
static_assert(a1<short>() == 6);
struct S { int m; static constexpr unsigned long k = sizeof(m); void f() { static_assert(sizeof(m) == 4); } };
template <class T> struct TS { T m; static constexpr unsigned long k = sizeof(m); };
static_assert(TS<char>::k == 1);
auto lam = [] { return 1; };
static_assert(sizeof(lam) == 1 && alignof(decltype(lam)) == 1);
template <class T> constexpr auto pe(T t) -> decltype(sizeof(t)) { return sizeof(t); }
static_assert(pe(1L) == 8);
constexpr unsigned long fsz(int x) { return sizeof(int[3]) + x; }
static_assert(fsz(1) == 13);
template <unsigned long N> struct Aligned { alignas(N) char c; };
static_assert(sizeof(Aligned<sizeof(double) * 2>) == 16);
void g() noexcept(sizeof(int) == 4);
static_assert(noexcept(g()));
enum E : char { e = sizeof(int) };
static_assert(e == 4);
template <class T, class = decltype(sizeof(T))> constexpr int ok(int) { return 1; }
template <class T> constexpr int ok(...) { return 0; }
struct Inc; 
static_assert(sizeof(int[sizeof(char) + 2]) == 12);
namespace N { struct Q { char c[7]; }; }
static_assert(sizeof(N::Q) == 7 && sizeof(::N::Q::c) == 7);
static_assert(sizeof(void *) == sizeof(void (*)()) && sizeof(void(*)(...)) == 8);
static_assert(__builtin_offsetof(N::Q, c) == 0);
static_assert(sizeof(__int128) == 16 && alignof(__int128) == 16);
static_assert(sizeof(long double) == 16 && sizeof(_Complex double) == 16);
int main() { return 0; }
