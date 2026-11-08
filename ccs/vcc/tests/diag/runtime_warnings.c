int a[4];
int f(int x)
{
	int y = x / 0;
	int z = x % 0;
	int s = x << 32;
	int t = x >> -1;
	int u = a[4];
	int v = a[-1];
	if (0)
		return 1 / 0;
	return y + z + s + t + u + v;
}
