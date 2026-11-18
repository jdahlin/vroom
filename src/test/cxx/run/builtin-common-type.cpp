/* [ext:clang_builtin_templates] __builtin_common_type<Template,
   TypeIdentity, Empty, T...>, as libc++'s common_type uses it:
   [meta.trans.other]/3's rules, a user's specialization reached through
   Template, Empty when there is no common type */
namespace s {
template <class T> struct ti { typedef T type; };
struct empty {};
template <class... A> struct common_type;
template <class... A> using ctt = typename common_type<A...>::type;
template <class... A>
struct common_type : __builtin_common_type<ctt, ti, empty, A...> {};
template <class...> using void_t = void;
template <class T, class = void> struct has_type { enum { value = 0 }; };
template <class T> struct has_type<T, void_t<typename T::type>> {
    enum { value = 1 };
};
} // namespace s

struct B {};
struct X {};
struct Y {};
namespace s {
/* a user's specialization, found for the decayed pair */
template <> struct common_type<X, Y> { typedef long type; };
} // namespace s

static_assert(sizeof(s::ctt<int, long>) == 8, "");
static_assert(sizeof(s::ctt<char, short, int>) == 4, "");
static_assert(sizeof(s::ctt<char>) == 1, "");
static_assert(sizeof(s::ctt<const X &, Y>) == 8, "");
static_assert(s::has_type<s::common_type<int, double>>::value, "");
static_assert(!s::has_type<s::common_type<void, int>>::value, "");
static_assert(!s::has_type<s::common_type<>>::value, "");
static_assert(sizeof(s::ctt<double, int>) == 8, "");

s::ctt<const int &, int> x = 3;
s::ctt<int[3], const int *> q = 0;
s::ctt<void, void> *nothing = 0;

int main()
{
    s::ctt<B, B> b;
    (void)b;
    if (x != 3 || q || nothing)
        return 1;
    s::ctt<char, unsigned char> c = 200;
    return c == 200 ? 0 : 2;
}
