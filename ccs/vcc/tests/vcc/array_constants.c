#ifdef __OPTIMIZE__
extern void unreachable_array_branch(void);
#else
static void unreachable_array_branch(void) { __builtin_trap(); }
#endif
static volatile int control;
static __attribute__((noinline)) void touch(void) { control++; }
static __attribute__((noinline)) void change(int *p) { *p = 9; }
int main(void)
{
	int a[2], b[1], c[1];
	a[0] = 0;
	touch();
	if (a[0]) unreachable_array_branch();
	a[0] = 1;
	for (int i = 0; i < 3; i++) a[0] = a[0] + 1;
	if (a[0] != 4) return 1;
	if (control) a[1] = 8; else a[1] = 3;
	if (a[1] != 8) return 2;
	b[0] = 2;
	change(&b[0]);
	if (b[0] != 9) return 3;
	c[0] = 5;
	goto check;
again:
	c[0] = c[0] - 1;
check:
	if (c[0]) goto again;
	if (c[0] != 0) return 4;
	return 0;
}
