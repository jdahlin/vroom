/* a declaration after a label, case or default (C23; an extension before) */
#include <stdio.h>

static int f(int c)
{
	int n = 0;
l:
	int x = c + n;
	if (++n < 3)
		goto l;
	switch (c) {
	case 1:
		int y = x * 2;
		return y;
	default:
		int z = x + 100;
		return z;
	}
}

int main(void)
{
	printf("%d %d\n", f(1), f(5));
	return 0;
}
