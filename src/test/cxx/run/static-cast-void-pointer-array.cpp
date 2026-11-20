// std: c++20
// [expr.static.cast]/13 with [expr.const.cast]/7: static_cast from "pointer to cv void" to a pointer to
// a pointer (or to a class, or a const-qualified type) in a class template whose realloc-grown array
// holds pointers; [conv.ptr]/2 back to void*; a reinterpret_cast that adds const below the first level
// with const at each level above
extern "C" void *malloc(unsigned long);
extern "C" void *realloc(void *, unsigned long);
extern "C" void free(void *);

template <class T> struct grown {
	T *elements = nullptr;
	unsigned long capacity = 0;
	~grown() { ::free(static_cast<void *>(elements)); }
	void reserve(unsigned long n)
	{
		T *p = static_cast<T *>(::realloc(static_cast<void *>(elements), n * sizeof(T)));
		elements = p;
		capacity = n;
	}
};

struct S { int a; };

int main()
{
	int r = 0, bit = 1;
#define T(x) do { if (!(x)) r |= bit; bit <<= 1; } while (0)
	static int v = 7;
	int *ip = &v;
	grown<int *> g;
	g.reserve(4);
	g.elements[3] = ip;
	T(*g.elements[3] == 7);
	grown<const char *> gc;
	gc.reserve(2);
	gc.elements[1] = "x";
	T(gc.elements[1][0] == 'x');
	grown<S> gs;
	gs.reserve(2);
	gs.elements[1].a = 5;
	T(gs.elements[1].a == 5);
	grown<S *> gsp;
	gsp.reserve(1);
	T(gsp.capacity == 1);
	const void *cv = &v;
	int *const *pp = static_cast<int *const *>(cv);
	T(pp == (int *const *)cv);
	int **ppi = &ip;
	const char *const *cc = reinterpret_cast<const char *const *>(ppi);
	T((const void *)cc == (const void *)ppi);
	const int **cpp = nullptr;
	void *vv = reinterpret_cast<void *>(cpp);
	T(vv == nullptr);
	return r;
}
