/* Bit-precise constant types, rank, layout and modular conversion. */
#define SAME(a, b) __builtin_types_compatible_p(__typeof__(a), b)
_Static_assert(SAME(42wb, _BitInt(7)), "signed literal width");
_Static_assert(SAME(42uwb, unsigned _BitInt(6)), "unsigned literal width");
_Static_assert(SAME(0wb, _BitInt(2)), "signed zero width");
_Static_assert(SAME(1WBU, unsigned _BitInt(1)), "unsigned width one");
_Static_assert(SAME(0xffffffffffffffffuwb, unsigned _BitInt(64)), "64-bit literal");
_Static_assert(SAME(0x10000000000000000wb, _BitInt(66)), "wide literal");
_Static_assert(SAME(+(_BitInt(5))0, _BitInt(5)), "no promotion");
_Static_assert(SAME((_BitInt(5))0 + (_BitInt(11))0, _BitInt(11)), "bitint rank");
_Static_assert(SAME(0 + (_BitInt(32))0, int), "standard rank wins tie");
_Static_assert(SAME(0 + (unsigned _BitInt(32))0, unsigned int), "unsigned conversion");
_Static_assert(SAME(0 + (unsigned _BitInt(31))0, int), "signed representability");
_Static_assert(!__builtin_types_compatible_p(_BitInt(32), int), "distinct type");
_Static_assert(sizeof(_BitInt(33)) == 8 && _Alignof(_BitInt(33)) == 8, "layout");
#if defined(__x86_64__) || defined(__wasm__)
_Static_assert(_Alignof(_BitInt(65)) == 8, "wide alignment");
#else
_Static_assert(_Alignof(_BitInt(65)) == 16, "arm wide alignment");
#endif
_Static_assert(sizeof(_BitInt(65)) == 16, "wide size");
_Static_assert((_BitInt(6))77 == 13, "signed truncation");
_Static_assert((_BitInt(6))63 == -1, "sign extension");
_Static_assert((unsigned _BitInt(6))-1 == 63, "unsigned truncation");
_Static_assert((unsigned _BitInt(6))63 + (unsigned _BitInt(6))2 == 1, "addition wraps at precision");
_Static_assert(~(unsigned _BitInt(5))0 == 31, "complement precision");
_Static_assert(((_BitInt(65))1 << 64) < 0, "wide sign bit");
static _BitInt(6) small = (_BitInt(6))77;
static _BitInt(6) negative = -1;
static unsigned _BitInt(65) wide = (unsigned _BitInt(65))1 << 64;
int main(void)
{
	const unsigned char *a = (const unsigned char *)&small;
	const unsigned char *b = (const unsigned char *)&negative;
	const unsigned char *c = (const unsigned char *)&wide;
	if (a[0] != 13 || b[0] != 255 || c[8] != 1)
		return 1;
	for (int i = 0; i < 16; i++)
		if (i != 8 && c[i])
			return 2;
	return 0;
}
