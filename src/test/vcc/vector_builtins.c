/* __builtin_shufflevector and __builtin_convertvector (TODO.md tier 3, builtins) */
#include <stdio.h>

typedef int v4si __attribute__((vector_size(16)));
typedef unsigned v4su __attribute__((vector_size(16)));
typedef float v4sf __attribute__((vector_size(16)));
typedef double v2df __attribute__((vector_size(16)));
typedef long long v2di __attribute__((vector_size(16)));
typedef int v2si __attribute__((vector_size(8)));
typedef signed char v8qi __attribute__((vector_size(8)));
typedef short v8hi __attribute__((vector_size(16)));

__attribute__((noinline)) v4si sh(v4si a, v4si b) { return __builtin_shufflevector(a, b, 3, 6, 0, 5); }
__attribute__((noinline)) v2si lo(v4si a) { return __builtin_shufflevector(a, a, 1, 2); }
__attribute__((noinline)) v8qi rev(v8qi a) { return __builtin_shufflevector(a, a, 7, 6, 5, 4, 3, 2, 1, 0); }
__attribute__((noinline)) v4si widen(v2si a, v2si b) { return __builtin_shufflevector(a, b, 0, 2, 1, 3); }
__attribute__((noinline)) v4sf i2f(v4si a) { return __builtin_convertvector(a, v4sf); }
__attribute__((noinline)) v4si f2i(v4sf a) { return __builtin_convertvector(a, v4si); }
__attribute__((noinline)) v4sf u2f(v4su a) { return __builtin_convertvector(a, v4sf); }
__attribute__((noinline)) v2df l2d(v2di a) { return __builtin_convertvector(a, v2df); }
__attribute__((noinline)) v8hi q2h(v8qi a) { return __builtin_convertvector(a, v8hi); }

/* vector_size on a parameter itself (it was dropped: sizeof 4) */
__attribute__((noinline)) int psum(int __attribute__((vector_size(16))) p)
{
	_Static_assert(sizeof(p) == 16, "vector parameter");
	return p[0] + p[1] + p[2] + p[3];
}

int main(void)
{
	v4si a = { 1, 2, 3, 4 }, b = { 5, 6, 7, 8 };
	v4si r = sh(a, b), w = widen((v2si){ 1, 2 }, (v2si){ 3, 4 });
	v2si l = lo(a);
	v8qi q = rev((v8qi){ 1, 2, 3, 4, 5, 6, 7, -8 });
	v4sf f = i2f(a), u = u2f((v4su){ 0, 1, 0x80000000u, 0xffffffffu });
	v4si g = f2i((v4sf){ 1.5f, -2.5f, 3.9f, 7 });
	v2df d = l2d((v2di){ -3, 1LL << 40 });
	v8hi h = q2h(q);

	printf("%d %d %d %d | %d %d | %d %d %d %d\n", r[0], r[1], r[2], r[3], l[0], l[1], w[0], w[1], w[2], w[3]);
	printf("%d %d %d\n", q[0], q[1], q[7]);
	printf("%g %g %g %g | %g %g\n", f[0], f[3], u[2], u[3], d[0], d[1]);
	printf("%d %d %d %d | %d %d\n", g[0], g[1], g[2], g[3], h[0], h[7]);
	printf("%d\n", psum(a));
	return 0;
}
