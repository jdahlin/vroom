#include "bf.h"

struct u0 mk_u0(int a, int b) { struct u0 r = { a, b }; return r; }
struct u3 mk_u3(int a, int s) { struct u3 r = { a, s }; return r; }
void fill_al(struct al *p, int a, int b, int c) { p->a = a; p->b = b; p->c = c; }
void fill_pk(struct pk *p, unsigned long w, int m, int z) { p->w = w; p->m = m; p->z = z; }
struct pk2 mk_pk2(long s, int t, int z) { struct pk2 r; r.q = 1; r.s = s; r.t = t; r.z = z; return r; }
long sum_all(struct u0 x, struct u3 y, struct al z, struct pk w, struct pk2 v, union un u)
{
	return x.a + x.b * 2 + y.a * 3 + y.s * 5 + z.a + z.b * 7 + z.c * 11 + (long)(w.w >> 40) + w.m + w.z +
	       v.q + (v.s >> 50) + v.t + v.z + u.a;
}
int sizes(void)
{
	return sizeof(struct u0) | sizeof(struct u3) << 4 | sizeof(struct al) << 8 | sizeof(struct pk) << 12 |
	       sizeof(struct pk2) << 18 | sizeof(union un) << 24 | _Alignof(struct u3) << 28;
}
