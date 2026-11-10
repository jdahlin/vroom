/* D8.2: -Wreturn-type shows one path to the closing brace as numbered steps: the false
   condition, the case the switch jumps to, the break that leaves a loop or switch */
int g(int);
int f1(int x)
{
	if (x > 0)
		return 1;
}
int f2(int x, int y)
{
	if (x) {
		if (y)
			return 1;
	} else
		return 2;
}
int f3(int x)
{
	switch (x) {
	case 1: return 1;
	case 2: return 2;
	}
}
int f4(int x)
{
	switch (x) {
	case 1: return 1;
	default: break;
	}
}
int f5(int x)
{
	while (x)
		if (g(x)) return 1;
}
int f6(int x)
{
	for (;;) {
		if (g(x))
			break;
		return 1;
	}
}
int f7(int x)
{
	do {
		if (g(x)) return 1;
	} while (--x);
}
int f8(int x)
{
	switch (x) {
	case 1: g(1);
	case 2: return 2;
	default: g(3);
	}
}
