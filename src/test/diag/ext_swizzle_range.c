int printf(const char *, ...);
typedef float f4 __attribute__((ext_vector_type(4)));
typedef float f2 __attribute__((ext_vector_type(2)));
typedef int i8 __attribute__((ext_vector_type(8)));
typedef float f3 __attribute__((ext_vector_type(3)));
int main(void) {
	f4 v = { 1, 2, 3, 4 };
	f2 a = v.xy, b = v.wz, h = v.hi, l = v.lo, ev = v.even, od = v.odd;
	f3 t = v.zyx;
	i8 w = { 0, 1, 2, 3, 4, 5, 6, 7 };
	v.y = 9;
	v.s3 = v.x + v.r;
	f4 sp = 2.5f;
	f4 q = v.wzyx * 2;
	printf("%g %g | %g %g | %g %g %g %g | %g %g %g | %g %g %g %g\n", a.x, a.y, b.x, b.y, h.y, l.x, ev.y, od.x, t.x, t.y, t.z, v.y, v.w, sp.z, q.x);
	printf("%d %d %d\n", w.s7, w.sA - 0 * w.s0 > 0 ? 1 : w.s5, w.even.s3);
	return 0;
}
