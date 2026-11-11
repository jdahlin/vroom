#ifdef __OPTIMIZE__
extern void dead_prefix(void);
#else
static void dead_prefix(void) { __builtin_trap(); }
#endif
static int result, calls;
static int next(void) { calls++; return 1; }
static void cases(int x)
{
	switch (x) {
	case 0:
		if (0) {
			dead_prefix();
	case 1:
			result++;
		}
	}
}
static void labels(void)
{
	goto inside;
	if (0) {
		dead_prefix();
		if (next()) {
inside:
			result += 2;
		} else {
			dead_prefix();
		}
	}
	goto other;
	if (1) {
		result += 8;
	} else {
		dead_prefix();
other:
		result += 4;
	}
}
int main(void)
{
	cases(0);
	if (result) return 1;
	cases(1);
	if (result != 1) return 2;
	labels();
	if (result != 7 || calls) return 3;
	return 0;
}
