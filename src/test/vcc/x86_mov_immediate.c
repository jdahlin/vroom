/* A 64-bit result can use a sign-extended imm32, a zero-extending
 * 32-bit register write, or a full imm64. Exercise both transitions. */
typedef unsigned long long word;
static volatile word expected[] = {
	0xffffffff7fffffffULL, 0xffffffff80000000ULL, 0x7fffffffULL,
	0x80000000ULL, 0xffffffffULL, 0x100000000ULL
};
__attribute__((noinline)) static word get(unsigned i)
{
	switch (i) {
	case 0: return 0xffffffff7fffffffULL;
	case 1: return 0xffffffff80000000ULL;
	case 2: return 0x7fffffffULL;
	case 3: return 0x80000000ULL;
	case 4: return 0xffffffffULL;
	default: return 0x100000000ULL;
	}
}
int main(void)
{
	for (unsigned i = 0; i < 6; i++)
		if (get(i) != expected[i]) return 1;
	return 0;
}
