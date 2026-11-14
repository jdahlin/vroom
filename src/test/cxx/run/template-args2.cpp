// templates as template arguments in names (basic_json<std::map, std::vector, ...>), functions in an
// unnamed namespace (internal), an overloaded function's address chosen by a cast's target type
template <class K, class V> struct Map { K k; V v; };
template <class T> struct Vec { T t; };
template <template <class, class> class M, template <class> class A> struct Doc { M<int, long> m; A<char> a; };
using D = Doc<Map, Vec>;
namespace {
int touch(D &d) { return (int)d.m.v + d.a.t; }
int touch(D &d, int x) { return (int)d.m.v + x; }
}
int main()
{
	D d;
	d.m.v = 40, d.a.t = 2;
	int (*p)(D &) = static_cast<int (*)(D &)>(&touch);
	int (*q)(D &, int) = static_cast<int (*)(D &, int)>(touch);
	return p(d) == 42 && q(d, 1) == 41 ? 0 : 1;
}
