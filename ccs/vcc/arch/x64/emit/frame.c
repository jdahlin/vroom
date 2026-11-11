/* Omit an x86 leaf frame only when no emitted operation depends on it.
   Fold the decision into the existing two-address traversal: no extra scan. */
#include "x64impl.h"

VPRIV bool x64_framefree_candidate(ecx *e)
{
	return e->E->o->omit_frame_pointer && !e->E->noopt && !e->x64->interrupt &&
	       !e->wk->foff && e->wk->falign <= 16 && !e->wk->spneed && !e->calls;
}

VPRIV bool x64_framefree_record(ecx *e, const rec *i)
{
	receffects f = x64_effects(e, i);
	rmask frame = (UINT64_C(1) << RBP) | (UINT64_C(1) << RSP);

	/* Includes frame/return-address builtins, stack arguments, calls, stack
	   adjustments and opaque asm. Unknown opcodes have conservative effects.
	   Two-address expansion only adds moves among these same registers. */
	return !((f.use | f.def) & frame) && !(f.fl & OD_CALL);
}
