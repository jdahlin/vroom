// -Wunused-value and -Wunused-comparison: clang's cases, locations and notes
struct S { int f; int a[2]; };
int g(int);
__attribute__((pure)) int pf(int);
__attribute__((const)) int cf(int);
volatile int vv;
#define M(a) a
#define BODY x + 1
#define CMP x == 1
void t(int x, int *p, struct S s, struct S *ps)
{
	x;
	x + 1;
	x == 1;
	x != 1;
	x < 1;
	(void)x;
	(void *)p;
	(long)x;
	*p;
	-x;
	!x;
	&x;
	ps->f;
	s.f;
	s.a[1];
	p[1];
	g(1);
	pf(1);
	cf(1);
	x ? 1 : 2;
	x ? g(1) : 2;
	x && g(1);
	x && x;
	(g(1), 0);
	(0, g(1));
	x = (1, g(1));
	sizeof(x);
	"str";
	'c';
	vv;
	({ 1; });
	({ g(1); });
	M(x + 1);
	BODY;
	CMP;
	for (x; x < 3; x + 1)
		;
	if (0) { x + 1; }
	x++, x;
}
