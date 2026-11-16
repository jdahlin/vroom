// const T (&)[N] from an int[3] (non-const elements, an lvalue or an rvalue such as declval<int[3]>()), in a call
// and in a detection idiom, and T x = arr of a class with a T&& constructor template (json j = nbs: to_json(j,
// const T(&arr)[N]) through has_to_json<json, int[3]>)
template <class T> T &&declval();
template <class T, unsigned long N> int f(const T (&arr)[N]) { return (int)N; }
template <class...> using void_t = void;
template <class A, class = void> struct has { static constexpr bool v = false; };
template <class A> struct has<A, void_t<decltype(f(declval<A>()))>> { static constexpr bool v = true; };
template <bool B> struct W { static constexpr int v = B ? 1 : 2; };
struct J { int n; template <class T> J(T &&t) : n(sizeof(t) / sizeof(t[0])) {} };
int main()
{
	int b[3] = {5, 0, 0};
	const int nbs[] = {0, 1, 2, 3};
	const J j = nbs;
	return f(b) == 3 && f(nbs) == 4 && W<has<int[3]>::v>::v == 1 && W<has<const int (&)[3]>::v>::v == 1 && W<has<int>::v>::v == 2 &&
	       j.n == 4 ? 0 : 1;
}
