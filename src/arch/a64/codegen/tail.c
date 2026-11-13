/* Bounded direct sibling calls. Park arguments before assigning ABI registers
 * so permutations never overwrite another argument's incoming value.
 */
#include "../../../codegen/emitimpl.h"

VPRIV bool a64_tail_return(ecx *e, uint32_t expr)
{
	cc *c = e->c;
	uint32_t callee;
	int64_t slots[8];
	val values[8];

	if (DARWIN(c) || e->E->o->opt < 1 || e->E->noopt || !e->f || e->f->inl || e->vararg ||
	    e->dyn || e->nclean != e->f->cbase ||
	    !em_tail_candidate(e, e->f->fn, expr, 8, &callee))
		return false;
	const node *call = N(expr);

	for (uint32_t i = 0; i < call->c; i++) {
		uint32_t arg = c->lists[call->b + i];

		values[i] = gen(e, arg);
		slots[i] = 0;
		if (values[i].k != V_CONST) {
			int r = toreg(e, &values[i]);

			slots[i] = slot(e, 8, 8);
			mem(e, I_ST, 3, 0, r, FRAME, slots[i]);
			val_free(e, &values[i]);
		}
	}
	for (uint32_t i = 0; i < call->c; i++) {
		qty ty = N(c->lists[call->b + i])->ty;
		int wide = tsize(c, ty) == 8;

		if (slots[i])
			mem(e, I_LD, wide ? 3 : 2, 0, (int)i, FRAME, slots[i]);
		else
			movi(e, wide, (int)i, (int64_t)em_trunc_to(c, values[i].i, ty));
	}
	uint32_t ref = REF(RK_SYM, callee);

	note_ref(e, ref);
	put(e, I_TAIL)->ref = ref;
	e->reach = false;
	return true;
}

/* Keep the zero-code terminal marker to preserve debug record indices. */
VPRIV uint32_t a64_tail_ref(const ecx *e)
{
	uint32_t k = e->nin;

	while (k && e->in[k - 1].op == I_LABEL)
		k--;
	return k && e->in[k - 1].op == I_TAIL && !e->in[k - 1].cond ? e->in[k - 1].ref : 0;
}
