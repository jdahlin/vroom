#ifdef __OPTIMIZE__
extern void unreachable_branch(void);
#else
static void unreachable_branch(void) { __builtin_trap(); }
#endif
static const double one = 1.0;
static int calls;
static int next(void) { return ++calls; }
static void compare(int x, int y)
{
	if ((x == y) && (x != y)) unreachable_branch();
	if ((x < y) && (y < x)) unreachable_branch();
	if (!((x <= y) || (y < x))) unreachable_branch();
	if (!(x <= 0 || (unsigned long)(unsigned)(x - 1) < 4294967295U)) unreachable_branch();
	if (x >> 0 != x || x << 0 != x) unreachable_branch();
	if ((0 << x) != 0 || (0 >> x) != 0 || (-1 >> x) != -1) unreachable_branch();
	if ((int)one != 1) unreachable_branch();
}
int main(void)
{
	compare(0, 2);
	compare(3, 1);
	if (!(next() == 1 && next() != 1) || calls != 2) return 1;
	if ((0 << (calls++, 2)) != 0 || calls != 3) return 2;
	return 0;
}
