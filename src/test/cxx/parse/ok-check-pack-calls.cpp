namespace std {
template <class T> T &&declval() noexcept;
template <class T, class U> struct is_same { static constexpr bool value = false; };
template <class T> struct is_same<T, T> { static constexpr bool value = true; };
template <class T> struct remove_reference { typedef T type; };
template <class T> struct remove_reference<T &> { typedef T type; };
template <class T> struct remove_reference<T &&> { typedef T type; };
template <class T> constexpr T &&forward(typename remove_reference<T>::type &t) noexcept { return static_cast<T &&>(t); }
}
struct ser { static int to_json(long &, int); static char to_json(long &, double *); };
template <typename T, typename... Args> using to_json_function = decltype(T::to_json(std::declval<Args>()...));
static_assert(std::is_same<to_json_function<ser, long &, int>, int>::value, "");
static_assert(std::is_same<to_json_function<ser, long &, double *>, char>::value, "");

int pick(int, long);
char pick(char *, long);
template <class... A> auto call(A &&...a) -> decltype(pick(std::forward<A>(a)...)) { return pick(std::forward<A>(a)...); }
static_assert(std::is_same<decltype(call(1, 2L)), int>::value, "");
static_assert(std::is_same<decltype(call((char *)0, 2L)), char>::value, "");

template <class... A> struct inv { template <class F> static auto go(F f, A... a) noexcept(noexcept(f(std::forward<A>(a)...))) -> decltype(f(std::forward<A>(a)...)); };
long fn(int, char);


struct mk { mk(int, char); };
template <class T, class... Args> T make(Args &&...args) { return T{static_cast<Args>(std::forward<Args>(args))...}; }
static_assert(std::is_same<decltype(make<mk>(1, 'c')), mk>::value, "");
void conv(long &, int);
template <class T> struct tmpl_ser { template <class J, class U> static auto to_json(J &j, U &&u) -> decltype(conv(j, std::forward<U>(u)), void()); };
template <class T, class... Args> using fn2 = decltype(T::to_json(std::declval<Args>()...));
static_assert(std::is_same<fn2<tmpl_ser<int>, long &, int>, void>::value, "");
