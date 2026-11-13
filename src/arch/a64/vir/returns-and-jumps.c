/* arm64 lowering of VIR: returns, jumps between blocks, the d8-d15 save and sibling-call candidates. */

#include "lowering-internal.h"

/* the block a branch to t ends up in: past blocks that are a lone br without moves */
VPRIV uint32_t vl_chase(vl *L, uint32_t t) { return L->chs[t]; }

/* a return: the epilogue and ret, or a jump to the shared one (finish_function decides) */
/* d8-d15 as the caller had them: at the entry into their slots, before every way out back */
VPRIV void vl_fcsave(vl *L, bool restore, int t)
{
	for (int r = 8; r < 16; r++)
		if (L->o->callee[1] >> r & 1) {
			if (restore) {
				mem(L->e, I_LD, 3, 0, t, FRAME, L->fcs[r - 8]);
				word(L->e, f_in(1, r, t));
			} else {
				word(L->e, f_out(1, t, r));
				mem(L->e, I_ST, 3, 0, t, FRAME, L->fcs[r - 8]);
			}
		}
}

VPRIV void vl_ret(vl *L)
{
	ecx *e = L->e;
	ins *t;

	if (!e->reach)
		return;
	vl_fcsave(L, true, R_X16);
	t = put(e, I_TAIL);
	t->cond = 4;
	t->sh = L->bare;
	t->label = L->retlbl;
	e->lrefc[L->retlbl]++;
	e->reach = false;
}

/*
 * Block t may be copied to the end of a block that jumps to it (tail duplication, as
 * LLVM's block placement): a return (its epilogue, or the jump to the shared one,
 * finish_function's choice), and a lone br (its edge's moves, then on) up to 8 deep.
 * Copies of blocks with work in them (a loop's test at the end of the latch) measured
 * more instructions on speedtest1: their constants and edge moves are lowered again.
 */
/* from block b's code, on to block t (an edge without moves left): a jump, or a copy of t */
VPRIV void vl_goto(vl *L, uint32_t b, uint32_t t)
{
	/* into a loop whose test is at its bottom (vl_order): a copy of the test here, so that
	 * the way in costs no jump (loop rotation with the guard, as LLVM's) */

	if (L->bare && vir_dominates(L->f, L->sp, t)) {	/* to the save point: its code runs behind the prologue */
		jump(L->e, L->lbl[t]);
		return;
	}
	if (vir_layout_duplicate(L->f, L->lp, VLT.hl, b, t, vl_fnext(L), L->dup, !L->e->E->noopt)) {
		int d = L->dup++;

		virbuild_lower_block(&L->drv, t);
		L->dup = d;
		return;
	}
	jump(L->e, vl_lbl(L, t));
}

/* the call at i will be a sibling call (vl_call's test, before the registers are known) */
VPRIV bool vl_sibcand(vl *L, uint32_t b, uint32_t i)
{
	const vir_fn *f = L->f;
	uint32_t ng = VSIG_NGROUPS(f->extra[f->ins[i].b]);
	vlarg *a = __builtin_alloca(sizeof(vlarg) * (ng + 1));
	int32_t stk;

	vl_assign(DARWIN(L->e->c), &f->extra[f->ins[i].b], a, ng, &stk);
	return !stk && vl_sibling(L, b, i);
}

/* instruction i of block b, its operands collected; res its result's register */
