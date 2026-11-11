static int calls;

static double once(double x)
{
	calls++;
	return x;
}

int main(void)
{
	volatile float f = -0.0f;
	volatile double d = 3.25;
	volatile long double l = 0.0L;
	volatile double nan = __builtin_nan("");

	if ((f ?: 2.5f) != 2.5f || (d ?: 9.0) != 3.25) return 1;
	if ((l ?: 7.0L) != 7.0L) return 2;
	l = 1.0L;
	if ((l ?: 7.0L) != 1.0L) return 3;
	if (!__builtin_isnan(nan ?: 5.0)) return 4;
	if ((once(0.0) ?: once(2.0)) != 2.0 || calls != 2) return 5;
	if ((once(4.0) ?: once(9.0)) != 4.0 || calls != 3) return 6;
	l = -0.0L;
	d = -0.0;
	if ((l ?: 7.0L) != 7.0L || (d ?: 3.0) != 3.0) return 7;
	if ((-0.0 ?: 3.0) != 3.0 || (-0.0L ?: 7.0L) != 7.0L) return 8;
	return 0;
}
