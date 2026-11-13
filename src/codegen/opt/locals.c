/*
 * locals.c: which locals may live in registers, and the choice of the most used ones, without
 * another walk of the AST.
 *
 * em_local_eligible lists the reasons a local must stay in memory (address taken, volatile, atomic, a
 * VLA, a cleanup, static or extern, a register variable bound to an asm name).  em_select_locals
 * takes the prepass's weighted reference counts (E->lspan, the per-symbol weights) and picks at most
 * `max` candidates that the target accepts (target_ok); equal weights keep symbol order, so the
 * choice is deterministic.  The caller owns the candidate array.  Used by arm64's function
 * generator, x86-64's locals pass, wasm and VIR's memory lowering.
 */
#include "../codegen-internal.h"

/* Target-independent reasons a local must retain an addressable memory object. */
VPRIV bool em_local_eligible(codegen_context *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	const attrs *a = codegen_symbol_attributes(c, s);
	ast_qualified_type t = y->ty;

	return codegen_symbol_is_local_variable(c, s) && y->sc != AST_STORAGE_STATIC && y->sc != AST_STORAGE_EXTERN &&
	       !e->E->lnoreg[s] && !(y->flags & AST_SYMBOL_FLAG_ADDRESS_TAKEN) && !(AST_QUALIFIED_TYPE_QUALIFIERS(t) & AST_QUALIFIER_VOLATILE) &&
	       !(T(c, t)->flags & (AST_TYPE_FLAG_VLA | AST_TYPE_FLAG_ATOMIC)) && !a->cleanup &&
	       !(y->sc == AST_STORAGE_REGISTER && a->asmname);
}

/* Native whole-function GPR bindings currently hold one integer/pointer word. */
VPRIV bool em_local_gpr_type(cc *c, ast_qualified_type t)
{
	return (codegen_type_is_integer(c, t) || codegen_type_is_pointer(c, t)) && !codegen_type_is_wide(c, t) && codegen_type_size(c, t) - 1 < 8;
}

/* Reuse the prepass's weighted references and symbol span. Equal weights retain
 * symbol order, including when the bounded list is full. The caller owns cand. */
VPRIV unsigned em_select_locals(codegen_context *e, uint32_t fn, uint32_t *cand, unsigned max,
			       bool (*target_ok)(cc *, ast_qualified_type))
{
	codegen_unit *E = e->E;
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
