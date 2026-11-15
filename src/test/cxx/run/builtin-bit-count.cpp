// __builtin_clzg/ctzg/popcountg in C++: the operand's own width, the value for 0 (libc++'s countl_zero, hash containers)
int main() {
	unsigned char c = 1; unsigned short s = 0; unsigned u = 8; unsigned long l = 1UL << 40; unsigned __int128 w = (unsigned __int128)1 << 100;
	return __builtin_clzg(c) == 7 && __builtin_clzg(s, 99) == 99 && __builtin_ctzg(u) == 3 && __builtin_clzg(l) == 23 &&
	       __builtin_ctzg(w) == 100 && __builtin_clzg(w) == 27 && __builtin_popcountg(u | 3u) == 3 && __builtin_ctzg(0u, -1) == -1 ? 0 : 1;
}
