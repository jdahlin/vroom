// floating-point classification builtins, source-location builtins, local arrays of unknown bound and
// char arrays from string literals
static int cls(double x) { return __builtin_isnan(x) | __builtin_isinf(x) << 1 | __builtin_isfinite(x) << 2 | __builtin_isnormal(x) << 3; }
static int clsf(float x) { return __builtin_isnan(x) | __builtin_isinf(x) << 1 | __builtin_isfinite(x) << 2 | __builtin_isnormal(x) << 3; }

int main()
{
	volatile double z = 0.0, one = 1.0;
	double v[] = { z / z, one / z, -one / z, 0.0, 1.5, 1e-310, -2.0 };
	int sum = 0;
	char s[] = "hey", t[8] = "ab";
	const char *w[] = { "a", "bc" };

	for (int i = 0; i < 7; i++)
		sum = sum * 16 + cls(v[i]);
	if (sum != 0x1224c4c || clsf(1e-40f) != 4 || clsf(3.0f) != 12)
		return 1;
	if (__builtin_LINE() != 18 || __builtin_FILE()[0] == 0 || sizeof v != 7 * sizeof(double))
		return 2;
	return sizeof s == 4 && s[1] == 'e' && t[1] == 'b' && t[5] == 0 && w[1][1] == 'c' ? 0 : 3;
}
