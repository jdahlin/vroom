/* -Wformat: typedef names in messages, size_t/intmax_t/ptrdiff_t length modifiers, char typedefs as integers */
typedef unsigned long size_t; typedef long ssize_t; typedef size_t myz; typedef unsigned char uint8_t; typedef long intmax_t; typedef long ptrdiff_t;
typedef const char *cstr; typedef int myint;
int pf(const char *f, ...) __attribute__((format(printf, 1, 2)));
struct S { size_t n; uint8_t b; };
void t(size_t z, ssize_t sz, myz m, uint8_t u, intmax_t im, ptrdiff_t pd, cstr cs, myint mi, struct S *p, const size_t cz) {
	pf("%d %d %d %s %d %d %d %ld", z, sz, m, u, im, pd, cs, mi);
	pf("%d %s %d", p->n, p->b, cz);
	pf("%s %c %x", u, z, u);
	pf("%*d", z, 1);
}
