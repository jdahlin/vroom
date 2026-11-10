/* D5.8: what attributes promise, checked at the uses: nonnull (-Wnonnull, plain, indexed
   and on a parameter), warn_unused_result (-Wunused-result), deprecated on functions, tags,
   typedefs, variables, enumerators and fields, unavailable (an error) */
__attribute__((nonnull)) void n1(int *a, int *b);
__attribute__((nonnull(2))) void n2(int *a, int *b);
void n3(int *a __attribute__((nonnull)), int *b);
__attribute__((warn_unused_result)) int w1(void);
__attribute__((deprecated)) void d1(void);
__attribute__((deprecated("use d3"))) void d2(void);
struct __attribute__((deprecated)) S { int x; };
typedef int T __attribute__((deprecated));
int dv __attribute__((deprecated));
enum { E1 __attribute__((deprecated)) = 1 };
__attribute__((unavailable("gone"))) void u1(void);
struct S2 { int f __attribute__((deprecated)); };
int g(void) {
	n1(0, 0); n2(0, 0); n2((void*)0, (int*)0); n3(0, 0);
	w1(); (void)w1();
	d1(); d2();
	struct S s; T t = dv + E1; struct S2 s2; s2.f = 1;
	return t + s.x;
}
#define NULL ((void*)0)
void h(void) { n1(NULL, 0); }
