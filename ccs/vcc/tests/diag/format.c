/* -Wformat: printf conversions against their arguments, ranges and fix-its */
int pf(const char *f, ...) __attribute__((format(printf, 1, 2)));
int vf(const char *f, __builtin_va_list) __attribute__((format(printf, 1, 0)));
enum U { U0 };
void t(long l, int i, char *s, enum U u, _Bool b, double d, __builtin_va_list ap) {
	pf("x=%d\n", l);
	pf("a %5.2s b", i);
	pf("%-08lx", s);
	pf("%" "d", l);
	pf("ab\tc%d", s);
	pf("%d %d", i);
	pf("%d", i, i, l);
	pf("%y", i);
	pf("abc %", i);
	pf("%*d", l, i);
	pf("%.*d", l, i);
	pf("%c", b);
	pf("%s", u);
	pf("%hhs", s);
	pf("%+s", s);
	pf("%%d %m", i);
	pf("%1$d", l);
	vf("%d %y", ap);
	pf(s);
	pf(s, i);
	pf("%.3c", i);
	pf("%lf %Lf", d, d);
}
