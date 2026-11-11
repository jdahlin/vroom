/* One shifted ADD for a profitable shared constant-multiply recipe. */
#include "../../../emit/emitimpl.h"

VPRIV bool a64_mulconst(ecx *e, qty ty, int r, i128 rhs)
{
	cc *c = e->c;
	if (e->E->o->opt < 1 || e->E->noopt || !is_integer(c, ty) || em_bitint(c, ty))
		return false;
	uint64_t size = tsize(c, ty);
	if (size != 4 && size != 8)
		return false;
	unsigned post = 0, shift = em_mulconst((unsigned)size * 8, rhs);

	if (!shift)
		shift = em_mulconst_shl((unsigned)size * 8, rhs, &post);
	if (!shift)
		return false;
	op3s(e, I_ADD, size == 8, r, r, r, 0, (int)shift);
	if (post)
		shift_imm(e, T_LESSLESS, size == 8, r, r, (int)post);
	return true;
}
