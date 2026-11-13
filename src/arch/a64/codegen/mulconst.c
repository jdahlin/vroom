/* One shifted ADD for a profitable shared constant-multiply recipe. */
#include "../../../codegen/codegen-internal.h"

VPRIV bool a64_mulconst(codegen_context *e, ast_qualified_type ty, int r, i128 rhs)
{
	cc *c = e->compile_context;
	if (e->unit->options->opt < 1 || e->unit->cleanups_disabled || !codegen_type_is_integer(c, ty) || codegen_type_is_bitint(c, ty))
		return false;
	uint64_t size = codegen_type_size(c, ty);
	if (size != 4 && size != 8)
		return false;
	unsigned shift = em_mulconst((unsigned)size * 8, rhs);
	if (!shift)
		return false;
	op3s(e, I_ADD, size == 8, r, r, r, 0, (int)shift);
	return true;
}
