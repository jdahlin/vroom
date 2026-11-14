typedef unsigned __int128 U;
typedef __int128 S;

__attribute__((noinline)) static U product(U a, U b) { return a * b; }
__attribute__((noinline)) static S signed_product(S a, S b) { return a * b; }

int main(void)
{
	U unit = (U)1 << 64;
	U mask = unit - 1;
	if (product(mask, mask) != (mask - 1) * unit + 1) return 1;
	if (product(2 * unit + 3, 5 * unit + 7) != 29 * unit + 21) return 2;
	if (product(unit, unit) != 0) return 3;
	if (signed_product(-((S)1 << 80), 7) != -((S)7 << 80)) return 4;
	return 0;
}
