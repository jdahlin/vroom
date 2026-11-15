// std: c++20
// __builtin_clzg/ctzg/popcountg (libc++'s __countl_zero, <bit>): lowered to the fixed-width builtins
int main()
{
	unsigned char c = 0x10;
	unsigned short s = 1;
	unsigned u = 0;
	unsigned long long l = 1ull << 40;
	if (__builtin_clzg(c) != 3 || __builtin_clzg(s) != 15 || __builtin_clzg(u, 32) != 32)
		return 1;
	if (__builtin_ctzg(l) != 40 || __builtin_ctzg(u, -1) != -1 || __builtin_popcountg(l | 7) != 4)
		return 2;
	if (__builtin_clzg((unsigned __int128)1) != 127 || __builtin_ctzg((unsigned __int128)1 << 100) != 100)
		return 3;
	return 0;
}
