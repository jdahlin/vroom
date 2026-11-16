// a constructor declared without a body, then a constructor template with one: the template's body is its own,
// and the first one's out-of-class definition stays its (libc++'s basic_ifstream(const string&), then
// template <class _Tp> basic_ifstream(const _Tp&) for filesystem::path)
struct Str { const char *s; };
struct Path { const char *c_str() const { return "p"; } };
template <class C> struct F {
	int how;
	explicit F(const char *s, int m = 1);
	explicit F(const Str &s, int m = 1);
	template <class Tp> explicit F(const Tp &p, int m = 1) : F(p.c_str(), m + 100) {}
};
template <class C> inline F<C>::F(const char *s, int m) : how(1 + m) {}
template <class C> inline F<C>::F(const Str &s, int m) : how(10 + m) {}
int main()
{
	F<char> a("y"), b(Str{"x"}), c(Path{});
	return a.how == 2 && b.how == 11 && c.how == 102 ? 0 : 1;
}
