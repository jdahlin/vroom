/* D6: --fix applies the machine-applicable fix-its only (a ';' missing at the end of a line,
   '.' for '->', an extra ';', a format flag clang ignores); the others stay (a guessed name,
   parentheses or '==' for an assignment, a conversion specification, a stray ')': perhaps
   a '(' was lost) */
/* diag-flags: -pedantic -Wno-error=implicit-function-declaration */
/* diag-fix */
struct point { int x, y; };;
int printf(const char *, ...);
int sum(struct point *p, struct point q, long n, int k) {
	int a = p.x + q->y
	printf("%+u %d\n", k, n);
	if (k = 2) a++;
	a = a + 1);
	return a + coutn;
}
