/* One scaled LEA for the shared 3/5/9 multiplication recipe. */
#include "x64impl.h"

VPRIV bool x64_mulconst(codegen_context *e, int size, int r, i128 rhs)
{
	if (e->E->o->opt < 1 || e->E->noopt || (size != 4 && size != 8))
		return false;
	unsigned shift = em_mulconst((unsigned)size * 8, rhs);
	if (!shift)
		return false;
	x64_op3(e, X_LEASCALE, size, r, r, r)->sh = (uint8_t)shift;
	return true;
}
