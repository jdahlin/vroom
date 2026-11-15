// the implicit operator= of a class with a member that has its own (no operator= declared here): each member
// assigned through its own operator=, not the bytes copied; a = "text" through a converting constructor's
// temporary, then that same implicit assignment (json's alt_string)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int copies, frees;
struct Buf {
	char *p;
	Buf() : p(nullptr) {}
	Buf(const char *s) : p(strdup(s)) {}
	Buf(const Buf &o) : p(o.p ? strdup(o.p) : nullptr) {}
	Buf &operator=(const Buf &o) { copies++; char *q = o.p ? strdup(o.p) : nullptr; free(p); p = q; return *this; }
	~Buf() { frees += p != nullptr; free(p); }
};
struct AS { Buf s; int n = 0; AS() {} AS(const char *x) : s(x), n(1) {} };
struct T2 { int a; double b; };
int main() {
	{
		AS x; x = "hello"; AS y; y = x;
		x.s.p[0] = 'j';
		T2 t{1, 2}, u{3, 4}; t = u;
		if (strcmp(x.s.p, "jello") || strcmp(y.s.p, "hello") || x.n != 1 || y.n != 1 || t.a != 3 || copies != 2)
			return 1;
	}
	return frees == 3 ? 0 : 2;
}
