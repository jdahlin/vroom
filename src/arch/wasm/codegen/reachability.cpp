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
	case AST_NODE_LABEL: case AST_NODE_CASE: case AST_NODE_DEFAULT:
		return true;
	case AST_NODE_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			if (wlabels(c, c->lists[x->b + i]))
				return true;
		return false;
	case AST_NODE_IF:
		return wlabels(c, x->b) || wlabels(c, x->c);
	case AST_NODE_WHILE: case AST_NODE_DO:
		return wlabels(c, x->b);
	case AST_NODE_FOR:
		return wlabels(c, c->lists[x->c + 1]);
	case AST_NODE_SWITCH:
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
	case AST_NODE_RETURN: case AST_NODE_BREAK: case AST_NODE_CONTINUE: case AST_NODE_GOTO: case AST_NODE_GOTO_INDIRECT:
		return true;
	case AST_NODE_BLOCK:
		return x->c && wterm(c, c->lists[x->b + x->c - 1]);
	case AST_NODE_IF:
		return x->c && wterm(c, x->b) && wterm(c, x->c);
	default:
		return false;
	}
}
