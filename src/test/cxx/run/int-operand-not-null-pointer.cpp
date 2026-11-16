// decltype(declval<int>() < declval<S>()) with only operator<(const char *, const S&): an int that is no
// constant 0 is no null pointer constant, so the expression is invalid (json's is_comparable<less<>,
// alt_string, int>, which made j["foo"][0] pick operator[](KeyType&&) with KeyType = int)
template <class T> T &&declval();
template <class...> using void_t = void;
template <class A, class B, class = void> struct cmp { static constexpr bool v = false; };
template <class A, class B> struct cmp<A, B, void_t<decltype(declval<A>() < declval<B>())>> { static constexpr bool v = true; };
struct S { int k; template <class T> bool operator<(const T &o) const noexcept { return k < o; } };
bool operator<(const char *a, const S &b) noexcept { return a == 0 && b.k > 0; }
template <bool B> struct W { static constexpr int v = B ? 1 : 2; };
int main()
{
	S s{1};
	return W<cmp<int, S>::v>::v == 2 && W<cmp<S, int>::v>::v == 1 && W<cmp<const char *, S>::v>::v == 1 && (0 < s) ? 0 : 1;
}
