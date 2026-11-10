/* D8.3: -Warray-bounds draws the array's cells and the index's (GCC's text art) */
struct S { int a[3]; int z; };
int f(struct S *p)
{
	int a[4];
	char b[2];
	long big[100];
	a[6] = 1;
	a[4] = 2;
	b[-1] = 0;
	big[100] = 0;
	a[-3] = 1;
	return p->a[5];
}
