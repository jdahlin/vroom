/*
 * mulconst.c: which constant multipliers a single shifted add can do (x * 3, 5 and 9 as x + (x << k)).
 *
 * em_mulconst(bits, rhs) returns k, or 0 for no recipe; 32- and 64-bit only.  Targets keep the
 * multiplication unless their machine has a profitable shifted-operand add (arm64 and x86-64's lea
 * use it; wasm keeps mul).  Used by arch/a64/codegen and arch/x64/codegen.
 */
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
