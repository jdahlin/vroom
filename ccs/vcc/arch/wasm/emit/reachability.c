/* Conservative AST reachability proofs for WebAssembly dead-code removal. */
#include "wimpl.h"

/* does statement n hold a label (a case, a goto target): it cannot be dropped */
VPRIV bool wlabels(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return false;
	x = N(n);
	switch (x->op) {
	case N_LABEL: case N_CASE: case N_DEFAULT:
		return true;
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			if (wlabels(c, c->lists[x->b + i]))
				return true;
		return false;
	case N_IF:
		return wlabels(c, x->b) || wlabels(c, x->c);
	case N_WHILE: case N_DO:
		return wlabels(c, x->b);
	case N_FOR:
		return wlabels(c, c->lists[x->c + 1]);
	case N_SWITCH:
		/* its case labels are its own */
		return false;
	default:
		return false;
	}
}

/* does statement n never complete normally (what follows it is dead until a label) */
VPRIV bool wterm(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return false;
	x = N(n);
	switch (x->op) {
	case N_RETURN: case N_BREAK: case N_CONTINUE: case N_GOTO: case N_GOTO_IND:
		return true;
	case N_BLOCK:
		return x->c && wterm(c, c->lists[x->b + x->c - 1]);
	case N_IF:
		return x->c && wterm(c, x->b) && wterm(c, x->c);
	default:
		return false;
	}
}
