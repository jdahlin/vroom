/* [expr.sizeof] in templates: a dependent operand is evaluated in each
   instantiation; in a template argument, an array bound, a partial
   specialization's argument, alignas, a requires-expression and a default
   argument; [temp.deduct]/8: an incomplete type in sizeof is a
   substitution failure, not an error; [temp.variadic]/7 and /8: sizeof...
   counts the elements of a pack */
template <class T> struct Arr { char a[sizeof(T)]; };
template <class T, unsigned long N = sizeof(T)> struct Def { static constexpr unsigned long n = N; };
template <class T, unsigned long = 0> inline const bool has_spec = false;
template <class T> inline const bool has_spec<T, sizeof(T) * 0> = true;
template <class T> struct Inc;
struct Complete {};
template <> struct Inc<char> {};
template <class T> constexpr unsigned long size_of() { return sizeof(T); }
template <class T> constexpr unsigned long align_of() { return alignof(T); }
template <class T> constexpr unsigned long of_expr(T t) { return sizeof(t) + sizeof t + sizeof(t + 1); }
template <class T> struct alignas(sizeof(T)) Al { char c; };
template <class... P> constexpr unsigned long count() { return sizeof...(P); }
template <class... P> constexpr unsigned long total() { return (sizeof(P) + ... + 0); }
template <class... P> constexpr unsigned long nth(unsigned long i) { constexpr unsigned long v[sizeof...(P) + 1] = { sizeof(P)..., 0 }; return v[i]; }
template <int N> constexpr unsigned long of_value() { return sizeof(N) + sizeof(char[N]); }
template <class T> concept Small = sizeof(T) <= 4;
template <class T> concept Sized = requires { sizeof(T); };
template <class T> constexpr int pick() { return 1; }
template <class T> requires Small<T> constexpr int pick() { return 2; }
template <class... A> constexpr unsigned long pack_args(A... a) { return sizeof...(a) * 100 + sizeof...(A); }
template <class T, class... R> constexpr unsigned long first_and_rest() { return sizeof(T) * 10 + sizeof...(R); }
template <class T> struct Member { T t; static constexpr unsigned long s = sizeof(t); static constexpr unsigned long m = sizeof(Member::t); };
template <class T> struct Nested { using type = T; static constexpr unsigned long n = sizeof(typename Nested<T>::type) + sizeof(type *); };
template <class T> struct Dep { typename T::type v; static constexpr unsigned long n = sizeof(typename T::type); };
struct HasType { using type = short; };
template <class T, class U> constexpr bool same_size = sizeof(T) == sizeof(U);

static_assert(sizeof(Arr<int>) == 4 && sizeof(Arr<double>) == 8);
static_assert(Def<long>::n == 8 && Def<char, 3>::n == 3);
static_assert(has_spec<Complete> && has_spec<Inc<char>> && !has_spec<Inc<int>>);
static_assert(size_of<long>() == 8 && size_of<Complete>() == 1);
static_assert(size_of<int[3]>() == 12 && size_of<int &>() == 4);
static_assert(align_of<double>() == 8 && align_of<int &>() == 4);
static_assert(of_expr(1) == 12 && of_expr('c') == 1 + 1 + 4);
static_assert(alignof(Al<int>) == 4 && sizeof(Al<int>) == 4 && alignof(Al<double>) == 8);
static_assert(count<>() == 0 && count<int, char, long>() == 3);
static_assert(total<>() == 0 && total<int, char, long>() == 13);
static_assert(nth<int, char, long>(2) == 8 && nth<>(0) == 0);
static_assert(of_value<5>() == 9);
static_assert(Small<char> && !Small<long> && Sized<int> && !Sized<Inc<int>>);
static_assert(pick<char>() == 2 && pick<long>() == 1);
static_assert(pack_args(1, 'c', 2.0) == 303);
static_assert(first_and_rest<long, int, int>() == 82);
static_assert(Member<int>::s == 4 && Member<char>::m == 1);
static_assert(Nested<int>::n == 12);
static_assert(Dep<HasType>::n == 2);
static_assert(same_size<int, unsigned> && !same_size<int, char>);
template <class T> int run() { T t{}; return sizeof(t) != sizeof(T); }
int main() { return run<int>() + run<Complete>() + run<double>(); }
