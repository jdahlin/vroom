/* Register-local eligibility and bounded weighted selection, without an AST walk. */
#include "../emitimpl.h"

/* Target-independent reasons a local must retain an addressable memory object. */
VPRIV bool em_local_eligible(ecx *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	const attrs *a = sym_attrs(c, s);
	qty t = y->ty;

	return is_local_var(c, s) && y->sc != SC_STATIC && y->sc != SC_EXTERN &&
	       !e->E->lnoreg[s] && !(y->flags & SF_ADDR_TAKEN) && !(QQ(t) & Q_VOLATILE) &&
	       !(T(c, t)->flags & (TF_VLA | TF_ATOMIC)) && !a->cleanup &&
	       !(y->sc == SC_REGISTER && a->asmname);
}

/* Native whole-function GPR bindings currently hold one integer/pointer word. */
VPRIV bool em_local_gpr_type(cc *c, qty t)
{
	return (is_integer(c, t) || is_ptr(c, t)) && !is_wide(c, t) && tsize(c, t) - 1 < 8;
}

/* Reuse the prepass's weighted references and symbol span. Equal weights retain
 * symbol order, including when the bounded list is full. The caller owns cand. */
VPRIV unsigned em_select_locals(ecx *e, uint32_t fn, uint32_t *cand, unsigned max,
			       bool (*target_ok)(cc *, qty))
{
	emitter *E = e->E;
	uint32_t lo = E->lspan[2 * fn], hi = E->lspan[2 * fn + 1];
	unsigned k = 0;

	if (!max || lo > hi)
		return 0;
	for (uint32_t s = lo; s <= hi; s++) {
		unsigned j;

		if (!E->luse[s] || em_unused_local(e, s, fn) || !em_local_eligible(e, s) || !target_ok(e->c, e->c->syms[s].ty))
			continue;
		if (k == max && E->luse[cand[k - 1]] >= E->luse[s])
			continue;
		j = k < max ? k++ : k - 1;
		while (j && E->luse[cand[j - 1]] < E->luse[s]) {
			cand[j] = cand[j - 1];
			j--;
		}
		cand[j] = s;
	}
	return k;
}
