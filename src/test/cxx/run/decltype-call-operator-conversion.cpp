// guessed-names: 2
// decltype(declval<F>()(declval<A>())) with F's operator() taking A only through a conversion (char to int):
// valid, so a function template using it is viable (libc++'s __invoke of json's lambda taking unsigned char)
template <class T> T &&declval();
namespace {	/* (vcc mangles a -> decltype(...) return type as v, clang the expression: local symbols) */
template <class F, class... A> auto inv(F &&f, A &&...a) -> decltype(declval<F>()(declval<A>()...)) { return f(static_cast<A &&>(a)...); }
}
struct G { bool operator()(int x) const { return x == 7; } };
int main()
{
	auto f = [](const unsigned char x) { return x == 7; };
	char c = 7;
	G g;
	return inv(g, c) && inv(f, c) && !inv(g, 'a') ? 0 : 1;
}
