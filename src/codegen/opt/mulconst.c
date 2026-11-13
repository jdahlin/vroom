/* Single shifted-add multiplication recipes. Targets retain multiplication
 * unless they can select a profitable machine operation (wasm keeps mul). */
#include "../emitimpl.h"

VPRIV unsigned em_mulconst(unsigned bits, i128 rhs)
{
	if (bits != 32 && bits != 64)
		return 0;
	switch (bits == 32 ? (uint32_t)rhs : (uint64_t)rhs) {
	case 3: return 1;
	case 5: return 2;
	case 9: return 3;
	default: return 0;
	}
}
