/* -Wformat: positional, invalid conversions, amounts, extra and missing arguments */
int pf(const char *f, ...) __attribute__((format(printf, 1, 2)));
struct S { int x; unsigned b:3; };
void t(long l, int i, char *s, struct S *p, char buf[4], float f, short sh) {
	pf("");
	pf("%C %S", i, s);
	pf("%D %O %U", i, i, i);
	pf("%q", i);
	pf("%5%", i);
	pf("%d", buf);
	pf("%s", l + i * 2);
	pf("%d %d %d", i);
	pf("%2$d %1$s", s, l);
	pf("%1$d %d", i, i);
	pf("%b %B", i, i);
	pf("%d", p->b);
	pf("%d", f);
	pf("%*d", f, i);
	pf("%ls", (char*)s);
	pf("%P", i);
	pf("%Z", i);
	pf("\x25" "d", l);
	pf("%d", sh, "x");
	pf("%lld", 1);
	pf("%hd", 70000);
	pf("%c", 'a');
	pf("%5.3n", &i);
	pf("%*.*s", i, i, s, i);
	pf("%-*d", l, i);
	pf("%'d", i);
}
