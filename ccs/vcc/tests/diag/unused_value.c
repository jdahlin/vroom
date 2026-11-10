int g(void); int x, *p, a[3];
struct S { int m; } s;
#define M(v) (v)
void f(int y) {
	x;
	1;
	x + 1;
	(0, g());
	g(), x;
	-x;
	*p;
	a[1];
	s.m;
	(void)x;
	x == 1;
	(int)x;
	y ? 1 : 2;
	y ? g() : 0;
	&x;
	sizeof x;
	"str";
	(x);
	~x;
	!x;
	x, x;
	({ x; });
	({ x; }) + 1;
	x++;
	x = 1, 2;
}
