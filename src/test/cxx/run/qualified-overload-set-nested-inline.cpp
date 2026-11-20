// std: c++20
// [namespace.qual]/1, [temp.over]/1, [over.match.call]: a qualified call
// of std::swap, an overload set of function templates and functions in a
// namespace that has nested inline namespaces (libc++'s std): the nested
// inline namespaces declare no swap, so the set is the namespace's own
// and resolved (libc++ <__exception/exception_ptr.h>: std::swap(tmp, *this))
extern "C" int printf(const char *, ...);
namespace ns {
inline namespace outer {
inline namespace inner {
int unrelated;
}
} // namespace outer
template <class T> void swap(T &a, T &b)
{
	T c = a;
	a = b;
	b = c;
}
template <class T, int N> void swap(T (&a)[N], T (&b)[N])
{
	for (int i = 0; i < N; i++)
		swap(a[i], b[i]);
}
void swap(char &a, char &b)
{
	char c = a;
	a = b;
	b = c;
	a++;
}
struct S {
	int v;
	S &operator=(S o)
	{
		v = o.v;
		return *this;
	}
};
struct H {
	int v;
	H &set(int x)
	{
		H tmp;
		tmp.v = x;
		ns::swap(tmp, *this);
		return *this;
	}
};
} // namespace ns
int main()
{
	int a = 1, b = 2;
	ns::swap(a, b);
	char c = 'a', d = 'b';
	ns::swap(c, d);
	int x[2] = {1, 2}, y[2] = {3, 4};
	ns::swap(x, y);
	ns::H h;
	h.v = 7;
	h.set(9);
	printf("%d %d %c %c %d %d %d\n", a, b, c, d, x[0], y[1], h.v);
	return 0;
}
