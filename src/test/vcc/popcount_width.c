/* Count each width with a nonconstant input. */
__attribute__((noinline)) static int count32(unsigned x) { return __builtin_popcount(x); }
__attribute__((noinline)) static int count64(unsigned long long x) { return __builtin_popcountll(x); }
__attribute__((noinline)) static int parity64(unsigned long long x) { return __builtin_parityll(x); }

int main(void)
{
	unsigned long long x = 0xaaaaaaaa00000001ULL;
	for (int i = 0; i < 65; ++i) {
		int low = 0, all = 0;
		for (int bit = 0; bit < 64; ++bit) {
			int set = (x >> bit) & 1;
			all += set;
			if (bit < 32) low += set;
		}
		if (count64(x) != all || count32((unsigned)x) != low || parity64(x) != (all & 1)) return 1;
		x <<= 1;
	}
	return 0;
}
