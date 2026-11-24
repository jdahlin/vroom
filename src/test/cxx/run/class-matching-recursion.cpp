/* [temp.inst]/2: a partial specialization's argument that needs the
   specialization being matched sees it incomplete (a substitution failure),
   as libc++'s __is_ostreamable with its operator<< does. */
struct yes_type {};
struct no_type {};
template <class T, T v> struct constant {
    static constexpr T value = v;
};
typedef constant<bool, true> true_c;
typedef constant<bool, false> false_c;
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <class T> T &&declval();
template <class A, class B, class = void> struct streamable : false_c {};
struct stream {};
struct item {};
template <class S, class T,
          typename enable_if<streamable<S &, const T &>::value, int>::type = 0>
S &operator<<(S &s, const T &)
{
    return s;
}
template <class S, class T>
struct streamable<S, T,
                  decltype(declval<S>() << declval<T>(), (void)0)> : true_c {};
int main()
{
    return streamable<stream &, const item &>::value ? 1 : 0;
}
