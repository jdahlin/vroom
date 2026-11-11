/* __int128 shifts by every count 0..127 (x86-64: inline, no __ashlti3/__lshrti3/__ashrti3, as clang) */
int printf(const char *, ...);
typedef unsigned __int128 u128; typedef __int128 s128;
__attribute__((noinline)) u128 shl(u128 a, int c) { return a << c; }
__attribute__((noinline)) u128 shr(u128 a, int c) { return a >> c; }
__attribute__((noinline)) s128 sar(s128 a, int c) { return a >> c; }
int main(void) {
	u128 x = ((u128)0x0123456789abcdefULL << 64) | 0xfedcba9876543210ULL; s128 y = -(s128)x; unsigned long h = 0;
	for (int c = 0; c < 128; c++) { u128 r1 = shl(x, c), r2 = shr(x, c); s128 r3 = sar(y, c);
		h = h * 31 + (unsigned long)r1 + (unsigned long)(r1 >> 64); h = h * 31 + (unsigned long)r2 + (unsigned long)(r2 >> 64); h = h * 31 + (unsigned long)r3 + (unsigned long)((u128)r3 >> 64); }
	printf("%lx\n", h); return 0; }
