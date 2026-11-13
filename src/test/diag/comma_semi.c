/* ',' (or ':') typed for ';' at a line end: clang's typo recovery, one error each, no follow-ons */
typedef int T;
int g1 = 1,
int g2;
short p[5],
T g3;
int f(int x)
{
	int a = 1,
	if (x)
		a = 2,
	for (;;)
		break;
	x = -1,
	T t = x;
	a = x:
	return a,
}
/* valid: these commas continue */
int h(int x)
{
	int a = 1,
	    b = 2;
	x = a,
	b = 3;
	for (a = 0, b = 1; a < b; a++, b--)
		;
	return (void)x, a + b;
}
