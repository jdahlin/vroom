/* -Wformat: flags, precision and length modifiers; the flags the fix-its keep */
int pf(const char *f, ...) __attribute__((format(printf, 1, 2)));
enum E { E0 }; struct S;
void t(char vc, short vs, int vi, unsigned vu, long vl, unsigned long vul, double vd, long double vld, char *ps, void *pv, int *pi, _Bool vb, enum E ve, struct S *pS) {
	pf("%+5u", vs);
	pf("%#hd", vc);
	pf("% 'hd", vl);
	pf("% #5f", vi);
	pf("%#0hhd", vd);
	pf("%-08lx", pv);
	pf("%+12li", vul);
	pf("%.u", vl);
	pf("%12.s", vd);
	pf("%.*c", vi);
	pf("% #5.3s", ps);
	pf("%.3S", pi);
	pf("%lC", vd);
	pf("%zC", vi);
	pf("%Ln", pi);
	pf("%.*Ln", vi, pi);
	pf("\t%hf%k\n%%", vc, vld, pi);
	pf("%\n%d", vi, vl);
	pf("% c", vb);
	pf("%+ o", vu);
	pf("%-0n", pi);
	pf("%'e", vd);
	pf("%5.3n", pi);
	pf("%lc", vb);
	pf("%ls", ps);
}
