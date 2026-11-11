/* Exercise runtime classification, including signed zero and subnormals. */
#define CHECK(T, suffix, minimum, tiny) \
static int check_##suffix(void) \
{ \
	volatile T inputs[] = {0.0, -0.0, 1.0, -1.0, minimum, tiny, \
		__builtin_huge_val##suffix(), -__builtin_huge_val##suffix(), \
		__builtin_nan##suffix("")}; \
	for (int i = 0; i < 9; i++) { \
		T x = inputs[i]; \
		if (!!__builtin_isfinite(x) != (i < 6)) return 1; \
		if (!!__builtin_isnormal(x) != (i >= 2 && i <= 4)) return 2; \
		if (!!__builtin_isinf(x) != (i == 6 || i == 7)) return 3; \
		if (!!__builtin_isnan(x) != (i == 8)) return 4; \
		if (i < 8 && !!__builtin_signbit(x) != (i == 1 || i == 3 || i == 7)) return 5; \
	} \
	return 0; \
}

CHECK(float, f, __FLT_MIN__, __FLT_DENORM_MIN__)
CHECK(double, , __DBL_MIN__, __DBL_DENORM_MIN__)
CHECK(long double, l, __LDBL_MIN__, __LDBL_DENORM_MIN__)

int main(void)
{
	return check_f() || check_() || check_l();
}
