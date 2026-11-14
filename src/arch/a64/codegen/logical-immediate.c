/* vcc code generator: instruction records, byte buffers, immediates, register names */
#include "../../../codegen/codegen-internal.h"

const char a64_condition_names[][8] = { "eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al" };

/* ---------------- immediates ---------------- */

/* AArch64 logical immediate: is v encodable for this width? */
VPRIV bool a64_logical_immediate_is_encodable(uint64_t v, int x)
{
	unsigned size = 64;

	if (!x)
		v = (v & 0xffffffffu) | (v << 32);
	if (v == 0 || v == ~(uint64_t)0)
		return false;
	/* smallest repeating element */
	while (size > 2) {
		unsigned h = size / 2;
		uint64_t m = (h == 64 ? ~(uint64_t)0 : (((uint64_t)1 << h) - 1));

		if (((v >> h) & m) != (v & m))
			break;
		size = h;
	}
	{
		uint64_t m = size == 64 ? ~(uint64_t)0 : (((uint64_t)1 << size) - 1);
		uint64_t e = v & m;
		/* e must be a rotated run of ones */
		uint64_t t = e;

		for (unsigned r = 0; r < size; r++) {
			/* contiguous ones starting at bit 0: t & (t + 1) == 0 */
			if ((t & (t + 1)) == 0)
				return true;
			t = ((t >> 1) | ((t & 1) << (size - 1))) & m;
		}
		return false;
	}
}
