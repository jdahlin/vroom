/* D5.5: -Wint-conversion with clang's hint and fix-it when '&' or '*' gives exactly the
   target type; illegal storage classes at file scope make the declaration invalid */
void fi(int);
void fp(int *);
register int r;
int f(void)
{
	int x = 0, *p = &x;
	int a = &x;
	int *q = x;
	long l = p;
	fi(p);
	fp(x);
	(void)q, (void)l;
	return a + r;
}
