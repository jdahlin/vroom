// __builtin_clzg / ctzg / popcountg (libc++'s countl_zero, std::sort's __log2i): lowered to the fixed-width
// builtins, the fallback for zero, the leading zeros of the operand's own width
int main()
{
	unsigned char b = 1;
	unsigned short h = 0x100;
	unsigned u = 0;
	unsigned long long w = 1ull << 40;
	int r = __builtin_clzg(b, 8) + __builtin_clzg(h, 16) * 10 + __builtin_clzg(u, 32) * 100 + __builtin_clzg(w) * 1000;
	int s = __builtin_ctzg(h) + __builtin_ctzg(u, 99) + __builtin_popcountg(w) + __builtin_popcountg(b);
	return r == 7 + 70 + 3200 + 23000 && s == 8 + 99 + 1 + 1 ? 0 : 1;
}
