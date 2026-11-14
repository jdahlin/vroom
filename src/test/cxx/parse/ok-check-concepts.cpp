// Concepts and requires: vcc must evaluate each static_assert as true (VCC_CXX_STATIC_ASSERT=1), clang accepts.
template<class T, class U> concept same = __is_same(T, U);
template<class T> concept integral = __is_integral(T);
template<class T> concept sized = requires(T t) { t.size(); };
template<class T> concept has_begin = requires(T &t) { { t.begin() } -> same<int *>; };
template<class T> concept adl_begin = requires(T &t) { begin(t); };
template<class T> concept small = requires { requires sizeof(T) <= 2; };
template<class T> concept has_type = requires { typename T::type; };
struct V { unsigned long size() const; int *begin(); typedef int type; };
struct W { char *begin(); };
namespace n { struct X {}; int begin(X &); }
static_assert(integral<int> && !integral<float>, "");
static_assert(same<int, int> && !same<int, long>, "");
static_assert(sized<V> && !sized<W> && !sized<int>, "");
static_assert(has_begin<V> && !has_begin<W>, "");
static_assert(adl_begin<n::X> && !adl_begin<V>, "");
static_assert(small<short> && !small<long>, "");
static_assert(has_type<V> && !has_type<W>, "");

// overloads chosen by constraints
template<integral T> char  f(T);
template<class T> requires (!integral<T>) short f(T);
template<class T> long g(T) requires sized<T>;
template<class T> char g(T) requires (!sized<T>);
template<class T> requires has_begin<T> int  h(T &);
template<class T> requires adl_begin<T> long h(T &);
void use(V v, n::X x, int i, double d)
{
	static_assert(sizeof(f(i)) == 1 && sizeof(f(d)) == 2, "");
	static_assert(sizeof(g(v)) == 8 && sizeof(g(i)) == 1, "");
	static_assert(sizeof(h(v)) == 4 && sizeof(h(x)) == 8, "");
}
