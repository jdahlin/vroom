/* -ffunction-sections -fdata-sections: one section per function/object
 * (.text.f, .data.d, .bss.b, .rodata.r, .tdata.t, .tbss.tb), linked and run */
/* vcc-flags: -ffunction-sections -fdata-sections */
int printf(const char *, ...);

int d = 1;
int b;
const int r = 2;
static int sb;
__thread int t = 4;
__thread int tb;
int com;
static const char *str = "hi";
__attribute__((section(".data.mine"))) int m = 3;

static int f(int x)
{
	static int sl;

	sl += x;
	return sl + sb + m;
}

int g(int x)
{
	return f(x) + d + b + r + t + tb + *str + com;
}

int main(void)
{
	int v;

	sb = 10, b = 20, tb = 30, com = 40;
	v = g(1) + g(2);
	printf("%d\n", v);
	return v != 432;	/* (1+10+3) + (3+10+3) + 2 * (1+20+2+4+30+'h'+40) */
}
